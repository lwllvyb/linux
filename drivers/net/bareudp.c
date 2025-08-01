// SPDX-License-Identifier: GPL-2.0
/* Bareudp: UDP  tunnel encasulation for different Payload types like
 * MPLS, NSH, IP, etc.
 * Copyright (c) 2019 Nokia, Inc.
 * Authors:  Martin Varghese, <martin.varghese@nokia.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/hash.h>
#include <net/dst_metadata.h>
#include <net/gro_cells.h>
#include <net/rtnetlink.h>
#include <net/protocol.h>
#include <net/ip6_tunnel.h>
#include <net/ip_tunnels.h>
#include <net/udp_tunnel.h>
#include <net/bareudp.h>

#define BAREUDP_BASE_HLEN sizeof(struct udphdr)
#define BAREUDP_IPV4_HLEN (sizeof(struct iphdr) + \
			   sizeof(struct udphdr))
#define BAREUDP_IPV6_HLEN (sizeof(struct ipv6hdr) + \
			   sizeof(struct udphdr))

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

/* per-network namespace private data for this module */

static unsigned int bareudp_net_id;

struct bareudp_net {
	struct list_head        bareudp_list;
};

struct bareudp_conf {
	__be16 ethertype;
	__be16 port;
	u16 sport_min;
	bool multi_proto_mode;
};

/* Pseudo network device */
struct bareudp_dev {
	struct net         *net;        /* netns for packet i/o */
	struct net_device  *dev;        /* netdev for bareudp tunnel */
	__be16		   ethertype;
	__be16             port;
	u16	           sport_min;
	bool               multi_proto_mode;
	struct socket      __rcu *sock;
	struct list_head   next;        /* bareudp node  on namespace list */
	struct gro_cells   gro_cells;
};

static int bareudp_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct metadata_dst *tun_dst = NULL;
	IP_TUNNEL_DECLARE_FLAGS(key) = { };
	struct bareudp_dev *bareudp;
	unsigned short family;
	unsigned int len;
	__be16 proto;
	void *oiph;
	int err;
	int nh;

	bareudp = rcu_dereference_sk_user_data(sk);
	if (!bareudp)
		goto drop;

	if (skb->protocol ==  htons(ETH_P_IP))
		family = AF_INET;
	else
		family = AF_INET6;

	if (bareudp->ethertype == htons(ETH_P_IP)) {
		__u8 ipversion;

		if (skb_copy_bits(skb, BAREUDP_BASE_HLEN, &ipversion,
				  sizeof(ipversion))) {
			dev_dstats_rx_dropped(bareudp->dev);
			goto drop;
		}
		ipversion >>= 4;

		if (ipversion == 4) {
			proto = htons(ETH_P_IP);
		} else if (ipversion == 6 && bareudp->multi_proto_mode) {
			proto = htons(ETH_P_IPV6);
		} else {
			dev_dstats_rx_dropped(bareudp->dev);
			goto drop;
		}
	} else if (bareudp->ethertype == htons(ETH_P_MPLS_UC)) {
		struct iphdr *tunnel_hdr;

		tunnel_hdr = (struct iphdr *)skb_network_header(skb);
		if (tunnel_hdr->version == 4) {
			if (!ipv4_is_multicast(tunnel_hdr->daddr)) {
				proto = bareudp->ethertype;
			} else if (bareudp->multi_proto_mode &&
				   ipv4_is_multicast(tunnel_hdr->daddr)) {
				proto = htons(ETH_P_MPLS_MC);
			} else {
				dev_dstats_rx_dropped(bareudp->dev);
				goto drop;
			}
		} else {
			int addr_type;
			struct ipv6hdr *tunnel_hdr_v6;

			tunnel_hdr_v6 = (struct ipv6hdr *)skb_network_header(skb);
			addr_type =
			ipv6_addr_type((struct in6_addr *)&tunnel_hdr_v6->daddr);
			if (!(addr_type & IPV6_ADDR_MULTICAST)) {
				proto = bareudp->ethertype;
			} else if (bareudp->multi_proto_mode &&
				   (addr_type & IPV6_ADDR_MULTICAST)) {
				proto = htons(ETH_P_MPLS_MC);
			} else {
				dev_dstats_rx_dropped(bareudp->dev);
				goto drop;
			}
		}
	} else {
		proto = bareudp->ethertype;
	}

	if (iptunnel_pull_header(skb, BAREUDP_BASE_HLEN,
				 proto,
				 !net_eq(bareudp->net,
				 dev_net(bareudp->dev)))) {
		dev_dstats_rx_dropped(bareudp->dev);
		goto drop;
	}

	__set_bit(IP_TUNNEL_KEY_BIT, key);

	tun_dst = udp_tun_rx_dst(skb, family, key, 0, 0);
	if (!tun_dst) {
		dev_dstats_rx_dropped(bareudp->dev);
		goto drop;
	}
	skb_dst_set(skb, &tun_dst->dst);
	skb->dev = bareudp->dev;
	skb_reset_mac_header(skb);

	/* Save offset of outer header relative to skb->head,
	 * because we are going to reset the network header to the inner header
	 * and might change skb->head.
	 */
	nh = skb_network_header(skb) - skb->head;

	skb_reset_network_header(skb);

	if (!pskb_inet_may_pull(skb)) {
		DEV_STATS_INC(bareudp->dev, rx_length_errors);
		DEV_STATS_INC(bareudp->dev, rx_errors);
		goto drop;
	}

	/* Get the outer header. */
	oiph = skb->head + nh;

	if (!ipv6_mod_enabled() || family == AF_INET)
		err = IP_ECN_decapsulate(oiph, skb);
	else
		err = IP6_ECN_decapsulate(oiph, skb);

	if (unlikely(err)) {
		if (log_ecn_error) {
			if  (!ipv6_mod_enabled() || family == AF_INET)
				net_info_ratelimited("non-ECT from %pI4 "
						     "with TOS=%#x\n",
						     &((struct iphdr *)oiph)->saddr,
						     ((struct iphdr *)oiph)->tos);
			else
				net_info_ratelimited("non-ECT from %pI6\n",
						     &((struct ipv6hdr *)oiph)->saddr);
		}
		if (err > 1) {
			DEV_STATS_INC(bareudp->dev, rx_frame_errors);
			DEV_STATS_INC(bareudp->dev, rx_errors);
			goto drop;
		}
	}

	len = skb->len;
	err = gro_cells_receive(&bareudp->gro_cells, skb);
	if (likely(err == NET_RX_SUCCESS))
		dev_dstats_rx_add(bareudp->dev, len);

	return 0;
drop:
	/* Consume bad packet */
	kfree_skb(skb);

	return 0;
}

static int bareudp_err_lookup(struct sock *sk, struct sk_buff *skb)
{
	return 0;
}

static int bareudp_init(struct net_device *dev)
{
	struct bareudp_dev *bareudp = netdev_priv(dev);
	int err;

	err = gro_cells_init(&bareudp->gro_cells, dev);
	if (err)
		return err;

	return 0;
}

static void bareudp_uninit(struct net_device *dev)
{
	struct bareudp_dev *bareudp = netdev_priv(dev);

	gro_cells_destroy(&bareudp->gro_cells);
}

static struct socket *bareudp_create_sock(struct net *net, __be16 port)
{
	struct udp_port_cfg udp_conf;
	struct socket *sock;
	int err;

	memset(&udp_conf, 0, sizeof(udp_conf));

	if (ipv6_mod_enabled())
		udp_conf.family = AF_INET6;
	else
		udp_conf.family = AF_INET;

	udp_conf.local_udp_port = port;
	/* Open UDP socket */
	err = udp_sock_create(net, &udp_conf, &sock);
	if (err < 0)
		return ERR_PTR(err);

	udp_allow_gso(sock->sk);
	return sock;
}

/* Create new listen socket if needed */
static int bareudp_socket_create(struct bareudp_dev *bareudp, __be16 port)
{
	struct udp_tunnel_sock_cfg tunnel_cfg;
	struct socket *sock;

	sock = bareudp_create_sock(bareudp->net, port);
	if (IS_ERR(sock))
		return PTR_ERR(sock);

	/* Mark socket as an encapsulation socket */
	memset(&tunnel_cfg, 0, sizeof(tunnel_cfg));
	tunnel_cfg.sk_user_data = bareudp;
	tunnel_cfg.encap_type = 1;
	tunnel_cfg.encap_rcv = bareudp_udp_encap_recv;
	tunnel_cfg.encap_err_lookup = bareudp_err_lookup;
	tunnel_cfg.encap_destroy = NULL;
	setup_udp_tunnel_sock(bareudp->net, sock, &tunnel_cfg);

	rcu_assign_pointer(bareudp->sock, sock);
	return 0;
}

static int bareudp_open(struct net_device *dev)
{
	struct bareudp_dev *bareudp = netdev_priv(dev);
	int ret = 0;

	ret =  bareudp_socket_create(bareudp, bareudp->port);
	return ret;
}

static void bareudp_sock_release(struct bareudp_dev *bareudp)
{
	struct socket *sock;

	sock = bareudp->sock;
	rcu_assign_pointer(bareudp->sock, NULL);
	synchronize_net();
	udp_tunnel_sock_release(sock);
}

static int bareudp_stop(struct net_device *dev)
{
	struct bareudp_dev *bareudp = netdev_priv(dev);

	bareudp_sock_release(bareudp);
	return 0;
}

static int bareudp_xmit_skb(struct sk_buff *skb, struct net_device *dev,
			    struct bareudp_dev *bareudp,
			    const struct ip_tunnel_info *info)
{
	bool udp_sum = test_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags);
	bool xnet = !net_eq(bareudp->net, dev_net(bareudp->dev));
	bool use_cache = ip_tunnel_dst_cache_usable(skb, info);
	struct socket *sock = rcu_dereference(bareudp->sock);
	const struct ip_tunnel_key *key = &info->key;
	struct rtable *rt;
	__be16 sport, df;
	int min_headroom;
	__u8 tos, ttl;
	__be32 saddr;
	int err;

	if (skb_vlan_inet_prepare(skb, skb->protocol != htons(ETH_P_TEB)))
		return -EINVAL;

	if (!sock)
		return -ESHUTDOWN;

	sport = udp_flow_src_port(bareudp->net, skb,
				  bareudp->sport_min, USHRT_MAX,
				  true);
	rt = udp_tunnel_dst_lookup(skb, dev, bareudp->net, 0, &saddr, &info->key,
				   sport, bareudp->port, key->tos,
				   use_cache ?
				   (struct dst_cache *)&info->dst_cache : NULL);

	if (IS_ERR(rt))
		return PTR_ERR(rt);

	skb_tunnel_check_pmtu(skb, &rt->dst,
			      BAREUDP_IPV4_HLEN + info->options_len, false);

	tos = ip_tunnel_ecn_encap(key->tos, ip_hdr(skb), skb);
	ttl = key->ttl;
	df = test_bit(IP_TUNNEL_DONT_FRAGMENT_BIT, key->tun_flags) ?
	     htons(IP_DF) : 0;
	skb_scrub_packet(skb, xnet);

	err = -ENOSPC;
	if (!skb_pull(skb, skb_network_offset(skb)))
		goto free_dst;

	min_headroom = LL_RESERVED_SPACE(rt->dst.dev) + rt->dst.header_len +
		BAREUDP_BASE_HLEN + info->options_len + sizeof(struct iphdr);

	err = skb_cow_head(skb, min_headroom);
	if (unlikely(err))
		goto free_dst;

	err = udp_tunnel_handle_offloads(skb, udp_sum);
	if (err)
		goto free_dst;

	skb_set_inner_protocol(skb, bareudp->ethertype);
	udp_tunnel_xmit_skb(rt, sock->sk, skb, saddr, info->key.u.ipv4.dst,
			    tos, ttl, df, sport, bareudp->port,
			    !net_eq(bareudp->net, dev_net(bareudp->dev)),
			    !test_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags),
			    0);
	return 0;

free_dst:
	dst_release(&rt->dst);
	return err;
}

static int bareudp6_xmit_skb(struct sk_buff *skb, struct net_device *dev,
			     struct bareudp_dev *bareudp,
			     const struct ip_tunnel_info *info)
{
	bool udp_sum = test_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags);
	bool xnet = !net_eq(bareudp->net, dev_net(bareudp->dev));
	bool use_cache = ip_tunnel_dst_cache_usable(skb, info);
	struct socket *sock  = rcu_dereference(bareudp->sock);
	const struct ip_tunnel_key *key = &info->key;
	struct dst_entry *dst = NULL;
	struct in6_addr saddr, daddr;
	int min_headroom;
	__u8 prio, ttl;
	__be16 sport;
	int err;

	if (skb_vlan_inet_prepare(skb, skb->protocol != htons(ETH_P_TEB)))
		return -EINVAL;

	if (!sock)
		return -ESHUTDOWN;

	sport = udp_flow_src_port(bareudp->net, skb,
				  bareudp->sport_min, USHRT_MAX,
				  true);
	dst = udp_tunnel6_dst_lookup(skb, dev, bareudp->net, sock, 0, &saddr,
				     key, sport, bareudp->port, key->tos,
				     use_cache ?
				     (struct dst_cache *) &info->dst_cache : NULL);
	if (IS_ERR(dst))
		return PTR_ERR(dst);

	skb_tunnel_check_pmtu(skb, dst, BAREUDP_IPV6_HLEN + info->options_len,
			      false);

	prio = ip_tunnel_ecn_encap(key->tos, ip_hdr(skb), skb);
	ttl = key->ttl;

	skb_scrub_packet(skb, xnet);

	err = -ENOSPC;
	if (!skb_pull(skb, skb_network_offset(skb)))
		goto free_dst;

	min_headroom = LL_RESERVED_SPACE(dst->dev) + dst->header_len +
		BAREUDP_BASE_HLEN + info->options_len + sizeof(struct ipv6hdr);

	err = skb_cow_head(skb, min_headroom);
	if (unlikely(err))
		goto free_dst;

	err = udp_tunnel_handle_offloads(skb, udp_sum);
	if (err)
		goto free_dst;

	daddr = info->key.u.ipv6.dst;
	udp_tunnel6_xmit_skb(dst, sock->sk, skb, dev,
			     &saddr, &daddr, prio, ttl,
			     info->key.label, sport, bareudp->port,
			     !test_bit(IP_TUNNEL_CSUM_BIT,
				       info->key.tun_flags),
			     0);
	return 0;

free_dst:
	dst_release(dst);
	return err;
}

static bool bareudp_proto_valid(struct bareudp_dev *bareudp, __be16 proto)
{
	if (bareudp->ethertype == proto)
		return true;

	if (!bareudp->multi_proto_mode)
		return false;

	if (bareudp->ethertype == htons(ETH_P_MPLS_UC) &&
	    proto == htons(ETH_P_MPLS_MC))
		return true;

	if (bareudp->ethertype == htons(ETH_P_IP) &&
	    proto == htons(ETH_P_IPV6))
		return true;

	return false;
}

static netdev_tx_t bareudp_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bareudp_dev *bareudp = netdev_priv(dev);
	struct ip_tunnel_info *info = NULL;
	int err;

	if (!bareudp_proto_valid(bareudp, skb->protocol)) {
		err = -EINVAL;
		goto tx_error;
	}

	info = skb_tunnel_info(skb);
	if (unlikely(!info || !(info->mode & IP_TUNNEL_INFO_TX))) {
		err = -EINVAL;
		goto tx_error;
	}

	rcu_read_lock();
	if (ipv6_mod_enabled() && info->mode & IP_TUNNEL_INFO_IPV6)
		err = bareudp6_xmit_skb(skb, dev, bareudp, info);
	else
		err = bareudp_xmit_skb(skb, dev, bareudp, info);

	rcu_read_unlock();

	if (likely(!err))
		return NETDEV_TX_OK;
tx_error:
	dev_kfree_skb(skb);

	if (err == -ELOOP)
		DEV_STATS_INC(dev, collisions);
	else if (err == -ENETUNREACH)
		DEV_STATS_INC(dev, tx_carrier_errors);

	DEV_STATS_INC(dev, tx_errors);
	return NETDEV_TX_OK;
}

static int bareudp_fill_metadata_dst(struct net_device *dev,
				     struct sk_buff *skb)
{
	struct ip_tunnel_info *info = skb_tunnel_info(skb);
	struct bareudp_dev *bareudp = netdev_priv(dev);
	bool use_cache;
	__be16 sport;

	use_cache = ip_tunnel_dst_cache_usable(skb, info);
	sport = udp_flow_src_port(bareudp->net, skb,
				  bareudp->sport_min, USHRT_MAX,
				  true);

	if (!ipv6_mod_enabled() || ip_tunnel_info_af(info) == AF_INET) {
		struct rtable *rt;
		__be32 saddr;

		rt = udp_tunnel_dst_lookup(skb, dev, bareudp->net, 0, &saddr,
					   &info->key, sport, bareudp->port,
					   info->key.tos,
					   use_cache ? &info->dst_cache : NULL);
		if (IS_ERR(rt))
			return PTR_ERR(rt);

		ip_rt_put(rt);
		info->key.u.ipv4.src = saddr;
	} else if (ip_tunnel_info_af(info) == AF_INET6) {
		struct dst_entry *dst;
		struct in6_addr saddr;
		struct socket *sock = rcu_dereference(bareudp->sock);

		dst = udp_tunnel6_dst_lookup(skb, dev, bareudp->net, sock,
					     0, &saddr, &info->key,
					     sport, bareudp->port, info->key.tos,
					     use_cache ? &info->dst_cache : NULL);
		if (IS_ERR(dst))
			return PTR_ERR(dst);

		dst_release(dst);
		info->key.u.ipv6.src = saddr;
	} else {
		return -EINVAL;
	}

	info->key.tp_src = sport;
	info->key.tp_dst = bareudp->port;
	return 0;
}

static const struct net_device_ops bareudp_netdev_ops = {
	.ndo_init               = bareudp_init,
	.ndo_uninit             = bareudp_uninit,
	.ndo_open               = bareudp_open,
	.ndo_stop               = bareudp_stop,
	.ndo_start_xmit         = bareudp_xmit,
	.ndo_fill_metadata_dst  = bareudp_fill_metadata_dst,
};

static const struct nla_policy bareudp_policy[IFLA_BAREUDP_MAX + 1] = {
	[IFLA_BAREUDP_PORT]                = { .type = NLA_U16 },
	[IFLA_BAREUDP_ETHERTYPE]	   = { .type = NLA_U16 },
	[IFLA_BAREUDP_SRCPORT_MIN]         = { .type = NLA_U16 },
	[IFLA_BAREUDP_MULTIPROTO_MODE]     = { .type = NLA_FLAG },
};

/* Info for udev, that this is a virtual tunnel endpoint */
static const struct device_type bareudp_type = {
	.name = "bareudp",
};

/* Initialize the device structure. */
static void bareudp_setup(struct net_device *dev)
{
	dev->netdev_ops = &bareudp_netdev_ops;
	dev->needs_free_netdev = true;
	SET_NETDEV_DEVTYPE(dev, &bareudp_type);
	dev->features    |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_FRAGLIST;
	dev->features    |= NETIF_F_RXCSUM;
	dev->features    |= NETIF_F_GSO_SOFTWARE;
	dev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_FRAGLIST;
	dev->hw_features |= NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_GSO_SOFTWARE;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->mtu = ETH_DATA_LEN;
	dev->min_mtu = IPV4_MIN_MTU;
	dev->max_mtu = IP_MAX_MTU - BAREUDP_BASE_HLEN;
	dev->type = ARPHRD_NONE;
	netif_keep_dst(dev);
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->lltx = true;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->pcpu_stat_type = NETDEV_PCPU_STAT_DSTATS;
}

static int bareudp_validate(struct nlattr *tb[], struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	if (!data) {
		NL_SET_ERR_MSG(extack,
			       "Not enough attributes provided to perform the operation");
		return -EINVAL;
	}
	return 0;
}

static int bareudp2info(struct nlattr *data[], struct bareudp_conf *conf,
			struct netlink_ext_ack *extack)
{
	memset(conf, 0, sizeof(*conf));

	if (!data[IFLA_BAREUDP_PORT]) {
		NL_SET_ERR_MSG(extack, "port not specified");
		return -EINVAL;
	}
	if (!data[IFLA_BAREUDP_ETHERTYPE]) {
		NL_SET_ERR_MSG(extack, "ethertype not specified");
		return -EINVAL;
	}

	conf->port = nla_get_u16(data[IFLA_BAREUDP_PORT]);
	conf->ethertype = nla_get_u16(data[IFLA_BAREUDP_ETHERTYPE]);

	if (data[IFLA_BAREUDP_SRCPORT_MIN])
		conf->sport_min =  nla_get_u16(data[IFLA_BAREUDP_SRCPORT_MIN]);

	if (data[IFLA_BAREUDP_MULTIPROTO_MODE])
		conf->multi_proto_mode = true;

	return 0;
}

static struct bareudp_dev *bareudp_find_dev(struct bareudp_net *bn,
					    const struct bareudp_conf *conf)
{
	struct bareudp_dev *bareudp, *t = NULL;

	list_for_each_entry(bareudp, &bn->bareudp_list, next) {
		if (conf->port == bareudp->port)
			t = bareudp;
	}
	return t;
}

static int bareudp_configure(struct net *net, struct net_device *dev,
			     struct bareudp_conf *conf,
			     struct netlink_ext_ack *extack)
{
	struct bareudp_net *bn = net_generic(net, bareudp_net_id);
	struct bareudp_dev *t, *bareudp = netdev_priv(dev);
	int err;

	bareudp->net = net;
	bareudp->dev = dev;
	t = bareudp_find_dev(bn, conf);
	if (t) {
		NL_SET_ERR_MSG(extack, "Another bareudp device using the same port already exists");
		return -EBUSY;
	}

	if (conf->multi_proto_mode &&
	    (conf->ethertype != htons(ETH_P_MPLS_UC) &&
	     conf->ethertype != htons(ETH_P_IP))) {
		NL_SET_ERR_MSG(extack, "Cannot set multiproto mode for this ethertype (only IPv4 and unicast MPLS are supported)");
		return -EINVAL;
	}

	bareudp->port = conf->port;
	bareudp->ethertype = conf->ethertype;
	bareudp->sport_min = conf->sport_min;
	bareudp->multi_proto_mode = conf->multi_proto_mode;

	err = register_netdevice(dev);
	if (err)
		return err;

	list_add(&bareudp->next, &bn->bareudp_list);
	return 0;
}

static int bareudp_link_config(struct net_device *dev,
			       struct nlattr *tb[])
{
	int err;

	if (tb[IFLA_MTU]) {
		err = dev_set_mtu(dev, nla_get_u32(tb[IFLA_MTU]));
		if (err)
			return err;
	}
	return 0;
}

static void bareudp_dellink(struct net_device *dev, struct list_head *head)
{
	struct bareudp_dev *bareudp = netdev_priv(dev);

	list_del(&bareudp->next);
	unregister_netdevice_queue(dev, head);
}

static int bareudp_newlink(struct net_device *dev,
			   struct rtnl_newlink_params *params,
			   struct netlink_ext_ack *extack)
{
	struct net *link_net = rtnl_newlink_link_net(params);
	struct nlattr **data = params->data;
	struct nlattr **tb = params->tb;
	struct bareudp_conf conf;
	int err;

	err = bareudp2info(data, &conf, extack);
	if (err)
		return err;

	err = bareudp_configure(link_net, dev, &conf, extack);
	if (err)
		return err;

	err = bareudp_link_config(dev, tb);
	if (err)
		goto err_unconfig;

	return 0;

err_unconfig:
	bareudp_dellink(dev, NULL);
	return err;
}

static size_t bareudp_get_size(const struct net_device *dev)
{
	return  nla_total_size(sizeof(__be16)) +  /* IFLA_BAREUDP_PORT */
		nla_total_size(sizeof(__be16)) +  /* IFLA_BAREUDP_ETHERTYPE */
		nla_total_size(sizeof(__u16))  +  /* IFLA_BAREUDP_SRCPORT_MIN */
		nla_total_size(0)              +  /* IFLA_BAREUDP_MULTIPROTO_MODE */
		0;
}

static int bareudp_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct bareudp_dev *bareudp = netdev_priv(dev);

	if (nla_put_be16(skb, IFLA_BAREUDP_PORT, bareudp->port))
		goto nla_put_failure;
	if (nla_put_be16(skb, IFLA_BAREUDP_ETHERTYPE, bareudp->ethertype))
		goto nla_put_failure;
	if (nla_put_u16(skb, IFLA_BAREUDP_SRCPORT_MIN, bareudp->sport_min))
		goto nla_put_failure;
	if (bareudp->multi_proto_mode &&
	    nla_put_flag(skb, IFLA_BAREUDP_MULTIPROTO_MODE))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops bareudp_link_ops __read_mostly = {
	.kind           = "bareudp",
	.maxtype        = IFLA_BAREUDP_MAX,
	.policy         = bareudp_policy,
	.priv_size      = sizeof(struct bareudp_dev),
	.setup          = bareudp_setup,
	.validate       = bareudp_validate,
	.newlink        = bareudp_newlink,
	.dellink        = bareudp_dellink,
	.get_size       = bareudp_get_size,
	.fill_info      = bareudp_fill_info,
};

static __net_init int bareudp_init_net(struct net *net)
{
	struct bareudp_net *bn = net_generic(net, bareudp_net_id);

	INIT_LIST_HEAD(&bn->bareudp_list);
	return 0;
}

static void __net_exit bareudp_exit_rtnl_net(struct net *net,
					     struct list_head *dev_kill_list)
{
	struct bareudp_net *bn = net_generic(net, bareudp_net_id);
	struct bareudp_dev *bareudp, *next;

	list_for_each_entry_safe(bareudp, next, &bn->bareudp_list, next)
		bareudp_dellink(bareudp->dev, dev_kill_list);
}

static struct pernet_operations bareudp_net_ops = {
	.init = bareudp_init_net,
	.exit_rtnl = bareudp_exit_rtnl_net,
	.id   = &bareudp_net_id,
	.size = sizeof(struct bareudp_net),
};

static int __init bareudp_init_module(void)
{
	int rc;

	rc = register_pernet_subsys(&bareudp_net_ops);
	if (rc)
		goto out1;

	rc = rtnl_link_register(&bareudp_link_ops);
	if (rc)
		goto out2;

	return 0;
out2:
	unregister_pernet_subsys(&bareudp_net_ops);
out1:
	return rc;
}
late_initcall(bareudp_init_module);

static void __exit bareudp_cleanup_module(void)
{
	rtnl_link_unregister(&bareudp_link_ops);
	unregister_pernet_subsys(&bareudp_net_ops);
}
module_exit(bareudp_cleanup_module);

MODULE_ALIAS_RTNL_LINK("bareudp");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Varghese <martin.varghese@nokia.com>");
MODULE_DESCRIPTION("Interface driver for UDP encapsulated traffic");
