# SPDX-License-Identifier: GPL-2.0
#
# Makefile for x86 specific library files.
#

# Produces uninteresting flaky coverage.
KCOV_INSTRUMENT_delay.o	:= n

# KCSAN uses udelay for introducing watchpoint delay; avoid recursion.
KCSAN_SANITIZE_delay.o := n
ifdef CONFIG_KCSAN
# In case KCSAN+lockdep+ftrace are enabled, disable ftrace for delay.o to avoid
# lockdep -> [other libs] -> KCSAN -> udelay -> ftrace -> lockdep recursion.
CFLAGS_REMOVE_delay.o = $(CC_FLAGS_FTRACE)
endif

inat_tables_script = $(srctree)/arch/x86/tools/gen-insn-attr-x86.awk
inat_tables_maps = $(srctree)/arch/x86/lib/x86-opcode-map.txt
quiet_cmd_inat_tables = GEN     $@
      cmd_inat_tables = $(AWK) -f $(inat_tables_script) $(inat_tables_maps) > $@

$(obj)/inat-tables.c: $(inat_tables_script) $(inat_tables_maps)
	$(call cmd,inat_tables)

$(obj)/inat.o: $(obj)/inat-tables.c

clean-files := inat-tables.c

obj-$(CONFIG_SMP) += msr-smp.o cache-smp.o

lib-y := delay.o misc.o cmdline.o cpu.o
lib-y += usercopy_$(BITS).o usercopy.o getuser.o putuser.o
lib-y += memcpy_$(BITS).o
lib-y += pc-conf-reg.o
lib-$(CONFIG_ARCH_HAS_COPY_MC) += copy_mc.o copy_mc_64.o
lib-$(CONFIG_INSTRUCTION_DECODER) += insn.o inat.o insn-eval.o
lib-$(CONFIG_RANDOMIZE_BASE) += kaslr.o
lib-$(CONFIG_FUNCTION_ERROR_INJECTION)	+= error-inject.o
lib-$(CONFIG_MITIGATION_RETPOLINE) += retpoline.o

obj-y += msr.o msr-reg.o msr-reg-export.o hweight.o
obj-y += iomem.o

ifeq ($(CONFIG_X86_32),y)
        obj-y += atomic64_32.o
        lib-y += atomic64_cx8_32.o
        lib-y += checksum_32.o
        lib-y += strstr_32.o
        lib-y += string_32.o
        lib-y += memmove_32.o
        lib-y += cmpxchg8b_emu.o
ifneq ($(CONFIG_X86_CX8),y)
        lib-y += atomic64_386_32.o
endif
else
ifneq ($(CONFIG_GENERIC_CSUM),y)
        lib-y += csum-partial_64.o csum-copy_64.o csum-wrappers_64.o
endif
        lib-y += clear_page_64.o copy_page_64.o
        lib-y += memmove_64.o memset_64.o
        lib-y += copy_user_64.o copy_user_uncached_64.o
        lib-y += cmpxchg16b_emu.o
        lib-y += bhi.o
endif
