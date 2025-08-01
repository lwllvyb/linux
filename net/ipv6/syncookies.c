// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  IPv6 Syncookies implementation for the Linux kernel
 *
 *  Authors:
 *  Glenn Griffin	<ggriffin.kernel@gmail.com>
 *
 *  Based on IPv4 implementation by Andi Kleen
 *  linux/net/ipv4/syncookies.c
 */

#include <linux/tcp.h>
#include <linux/random.h>
#include <linux/siphash.h>
#include <linux/kernel.h>
#include <net/secure_seq.h>
#include <net/ipv6.h>
#include <net/tcp.h>

#define COOKIEBITS 24	/* Upper bits store count */
#define COOKIEMASK (((__u32)1 << COOKIEBITS) - 1)

static siphash_aligned_key_t syncookie6_secret[2];

/* RFC 2460, Section 8.3:
 * [ipv6 tcp] MSS must be computed as the maximum packet size minus 60 [..]
 *
 * Due to IPV6_MIN_MTU=1280 the lowest possible MSS is 1220, which allows
 * using higher values than ipv4 tcp syncookies.
 * The other values are chosen based on ethernet (1500 and 9k MTU), plus
 * one that accounts for common encap (PPPoe) overhead. Table must be sorted.
 */
static __u16 const msstab[] = {
	1280 - 60, /* IPV6_MIN_MTU - 60 */
	1480 - 60,
	1500 - 60,
	9000 - 60,
};

static u32 cookie_hash(const struct in6_addr *saddr,
		       const struct in6_addr *daddr,
		       __be16 sport, __be16 dport, u32 count, int c)
{
	const struct {
		struct in6_addr saddr;
		struct in6_addr daddr;
		u32 count;
		__be16 sport;
		__be16 dport;
	} __aligned(SIPHASH_ALIGNMENT) combined = {
		.saddr = *saddr,
		.daddr = *daddr,
		.count = count,
		.sport = sport,
		.dport = dport
	};

	net_get_random_once(syncookie6_secret, sizeof(syncookie6_secret));
	return siphash(&combined, offsetofend(typeof(combined), dport),
		       &syncookie6_secret[c]);
}

static __u32 secure_tcp_syn_cookie(const struct in6_addr *saddr,
				   const struct in6_addr *daddr,
				   __be16 sport, __be16 dport, __u32 sseq,
				   __u32 data)
{
	u32 count = tcp_cookie_time();
	return (cookie_hash(saddr, daddr, sport, dport, 0, 0) +
		sseq + (count << COOKIEBITS) +
		((cookie_hash(saddr, daddr, sport, dport, count, 1) + data)
		& COOKIEMASK));
}

static __u32 check_tcp_syn_cookie(__u32 cookie, const struct in6_addr *saddr,
				  const struct in6_addr *daddr, __be16 sport,
				  __be16 dport, __u32 sseq)
{
	__u32 diff, count = tcp_cookie_time();

	cookie -= cookie_hash(saddr, daddr, sport, dport, 0, 0) + sseq;

	diff = (count - (cookie >> COOKIEBITS)) & ((__u32) -1 >> COOKIEBITS);
	if (diff >= MAX_SYNCOOKIE_AGE)
		return (__u32)-1;

	return (cookie -
		cookie_hash(saddr, daddr, sport, dport, count - diff, 1))
		& COOKIEMASK;
}

u32 __cookie_v6_init_sequence(const struct ipv6hdr *iph,
			      const struct tcphdr *th, __u16 *mssp)
{
	int mssind;
	const __u16 mss = *mssp;

	for (mssind = ARRAY_SIZE(msstab) - 1; mssind ; mssind--)
		if (mss >= msstab[mssind])
			break;

	*mssp = msstab[mssind];

	return secure_tcp_syn_cookie(&iph->saddr, &iph->daddr, th->source,
				     th->dest, ntohl(th->seq), mssind);
}
EXPORT_SYMBOL_GPL(__cookie_v6_init_sequence);

__u32 cookie_v6_init_sequence(const struct sk_buff *skb, __u16 *mssp)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);

	return __cookie_v6_init_sequence(iph, th, mssp);
}

int __cookie_v6_check(const struct ipv6hdr *iph, const struct tcphdr *th)
{
	__u32 cookie = ntohl(th->ack_seq) - 1;
	__u32 seq = ntohl(th->seq) - 1;
	__u32 mssind;

	mssind = check_tcp_syn_cookie(cookie, &iph->saddr, &iph->daddr,
				      th->source, th->dest, seq);

	return mssind < ARRAY_SIZE(msstab) ? msstab[mssind] : 0;
}
EXPORT_SYMBOL_GPL(__cookie_v6_check);

static struct request_sock *cookie_tcp_check(struct net *net, struct sock *sk,
					     struct sk_buff *skb)
{
	struct tcp_options_received tcp_opt;
	u32 tsoff = 0;
	int mss;

	if (tcp_synq_no_recent_overflow(sk))
		goto out;

	mss = __cookie_v6_check(ipv6_hdr(skb), tcp_hdr(skb));
	if (!mss) {
		__NET_INC_STATS(net, LINUX_MIB_SYNCOOKIESFAILED);
		goto out;
	}

	__NET_INC_STATS(net, LINUX_MIB_SYNCOOKIESRECV);

	/* check for timestamp cookie support */
	memset(&tcp_opt, 0, sizeof(tcp_opt));
	tcp_parse_options(net, skb, &tcp_opt, 0, NULL);

	if (tcp_opt.saw_tstamp && tcp_opt.rcv_tsecr) {
		tsoff = secure_tcpv6_ts_off(net,
					    ipv6_hdr(skb)->daddr.s6_addr32,
					    ipv6_hdr(skb)->saddr.s6_addr32);
		tcp_opt.rcv_tsecr -= tsoff;
	}

	if (!cookie_timestamp_decode(net, &tcp_opt))
		goto out;

	return cookie_tcp_reqsk_alloc(&tcp6_request_sock_ops, sk, skb,
				      &tcp_opt, mss, tsoff);
out:
	return ERR_PTR(-EINVAL);
}

struct sock *cookie_v6_check(struct sock *sk, struct sk_buff *skb)
{
	const struct tcphdr *th = tcp_hdr(skb);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct inet_request_sock *ireq;
	struct net *net = sock_net(sk);
	struct request_sock *req;
	struct dst_entry *dst;
	struct sock *ret = sk;
	__u8 rcv_wscale;
	int full_space;
	SKB_DR(reason);

	if (!READ_ONCE(net->ipv4.sysctl_tcp_syncookies) ||
	    !th->ack || th->rst)
		goto out;

	if (cookie_bpf_ok(skb)) {
		req = cookie_bpf_check(sk, skb);
	} else {
		req = cookie_tcp_check(net, sk, skb);
		if (IS_ERR(req))
			goto out;
	}
	if (!req) {
		SKB_DR_SET(reason, NO_SOCKET);
		goto out_drop;
	}

	ireq = inet_rsk(req);

	ireq->ir_v6_rmt_addr = ipv6_hdr(skb)->saddr;
	ireq->ir_v6_loc_addr = ipv6_hdr(skb)->daddr;

	if (security_inet_conn_request(sk, skb, req)) {
		SKB_DR_SET(reason, SECURITY_HOOK);
		goto out_free;
	}

	if (ipv6_opt_accepted(sk, skb, &TCP_SKB_CB(skb)->header.h6) ||
	    np->rxopt.bits.rxinfo || np->rxopt.bits.rxoinfo ||
	    np->rxopt.bits.rxhlim || np->rxopt.bits.rxohlim) {
		refcount_inc(&skb->users);
		ireq->pktopts = skb;
	}

	/* So that link locals have meaning */
	if (!sk->sk_bound_dev_if &&
	    ipv6_addr_type(&ireq->ir_v6_rmt_addr) & IPV6_ADDR_LINKLOCAL)
		ireq->ir_iif = tcp_v6_iif(skb);

	tcp_ao_syncookie(sk, skb, req, AF_INET6);

	/*
	 * We need to lookup the dst_entry to get the correct window size.
	 * This is taken from tcp_v6_syn_recv_sock.  Somebody please enlighten
	 * me if there is a preferred way.
	 */
	{
		struct in6_addr *final_p, final;
		struct flowi6 fl6;
		memset(&fl6, 0, sizeof(fl6));
		fl6.flowi6_proto = IPPROTO_TCP;
		fl6.daddr = ireq->ir_v6_rmt_addr;
		final_p = fl6_update_dst(&fl6, rcu_dereference(np->opt), &final);
		fl6.saddr = ireq->ir_v6_loc_addr;
		fl6.flowi6_oif = ireq->ir_iif;
		fl6.flowi6_mark = ireq->ir_mark;
		fl6.fl6_dport = ireq->ir_rmt_port;
		fl6.fl6_sport = inet_sk(sk)->inet_sport;
		fl6.flowi6_uid = sk_uid(sk);
		security_req_classify_flow(req, flowi6_to_flowi_common(&fl6));

		dst = ip6_dst_lookup_flow(net, sk, &fl6, final_p);
		if (IS_ERR(dst)) {
			SKB_DR_SET(reason, IP_OUTNOROUTES);
			goto out_free;
		}
	}

	req->rsk_window_clamp = READ_ONCE(tp->window_clamp) ? :dst_metric(dst, RTAX_WINDOW);
	/* limit the window selection if the user enforce a smaller rx buffer */
	full_space = tcp_full_space(sk);
	if (sk->sk_userlocks & SOCK_RCVBUF_LOCK &&
	    (req->rsk_window_clamp > full_space || req->rsk_window_clamp == 0))
		req->rsk_window_clamp = full_space;

	tcp_select_initial_window(sk, full_space, req->mss,
				  &req->rsk_rcv_wnd, &req->rsk_window_clamp,
				  ireq->wscale_ok, &rcv_wscale,
				  dst_metric(dst, RTAX_INITRWND));

	/* req->syncookie is set true only if ACK is validated
	 * by BPF kfunc, then, rcv_wscale is already configured.
	 */
	if (!req->syncookie)
		ireq->rcv_wscale = rcv_wscale;
	ireq->ecn_ok &= cookie_ecn_ok(net, dst);

	ret = tcp_get_cookie_sock(sk, skb, req, dst);
	if (!ret) {
		SKB_DR_SET(reason, NO_SOCKET);
		goto out_drop;
	}
out:
	return ret;
out_free:
	reqsk_free(req);
out_drop:
	sk_skb_reason_drop(sk, skb, reason);
	return NULL;
}
