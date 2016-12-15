/*
This is shamelessly ripped from Vine, because those guys have very very strange language preferences.
Vine is Copyright (C) 2006-2009, BitBlaze Team.

You can redistribute and modify it under the terms of the GNU GPL,
version 2 or later, but it is made available WITHOUT ANY WARRANTY.
See the top-level README file for more details.

For more information about Vine and other BitBlaze software, see our
web site at: http://bitblaze.cs.berkeley.edu/
*/

//======================================================================
//
// This file provides the interface to VEX that allows block by block
// translation from binary to VEX IR.
//
//======================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libvex.h>

#include "pyvex.h"
#include "logging.h"

// these are problematic because we need to link with vex statically to use them, I think
extern VexControl vex_control;
extern Bool vex_initdone;

// the last thrown error
char *last_error;

//======================================================================
//
// Globals
//
//======================================================================

// Some info required for translation
VexArchInfo         vai_host;
VexGuestExtents     vge;
VexTranslateArgs    vta;
VexTranslateResult  vtr;
VexAbiInfo	    vbi;
VexControl vc;

// Global for saving the intermediate results of translation from
// within the callback (instrument1)
IRSB *irbb_current = NULL;

//======================================================================
//
// Functions needed for the VEX translation
//
//======================================================================

static void failure_exit( void )
{
	printf("SHIIIIIT\n");
	exit(1);
}

static void log_bytes( const HChar* bytes, SizeT nbytes )
{
	SizeT i;
	for (i = 0; i < nbytes - 3; i += 4)
		printf("%c%c%c%c", bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]);
	for (; i < nbytes; i++)
		printf("%c", bytes[i]);
}

static Bool chase_into_ok( void *closureV, Addr addr64 )
{
	return False;
}

// TODO: figure out what this is for
static UInt needs_self_check(void *callback_opaque, VexRegisterUpdates* pxControl, const VexGuestExtents *guest_extents)
{
	return 0;
}

static void *dispatch(void)
{
	return NULL;
}

//----------------------------------------------------------------------
// This is where we copy out the IRSB
//----------------------------------------------------------------------
static IRSB *instrument1(  void *callback_opaque,
                           IRSB *irbb,
                           const VexGuestLayout *vgl,
                           const VexGuestExtents *vge,
                           const VexArchInfo *vae,
                           IRType gWordTy,
                           IRType hWordTy )
{

	assert(irbb);

	//irbb_current = (IRSB *)vx_dopyIRSB(irbb);
	irbb_current = deepCopyIRSB(irbb);

	if (debug_on) ppIRSB(irbb);
	return irbb;
}

//----------------------------------------------------------------------
// Initializes VEX
// It must be called before using VEX for translation to Valgrind IR
//----------------------------------------------------------------------
void vex_init()
{
	static int initialized = 0;
	debug("Initializing VEX.\n");

	if (initialized || vex_initdone)
	{
		debug("VEX already initialized.\n");
		return;
	}
	initialized = 1;

	//
	// Initialize VEX
	//
	LibVEX_default_VexControl(&vc);

	vc.iropt_verbosity              = 0;
	vc.iropt_level                  = 0;    // No optimization by default
	//vc.iropt_level                  = 2;
	//vc.iropt_precise_memory_exns    = False;
	vc.iropt_unroll_thresh          = 0;
	vc.guest_max_insns              = 1;    // By default, we vex 1 instruction at a time
	vc.guest_chase_thresh           = 0;

	debug("Calling LibVEX_Init()....\n");
	LibVEX_Init(&failure_exit,
	            &log_bytes,
	            0,              // Debug level
	            &vc );
	debug("LibVEX_Init() done....\n");

	LibVEX_default_VexArchInfo(&vai_host);
	LibVEX_default_VexAbiInfo(&vbi);

	vai_host.endness = VexEndnessLE; // TODO: Don't assume this

	// various settings to make stuff work
	// ... former is set to 'unspecified', but gets set in vex_inst for archs which care
	// ... the latter two are for dealing with gs and fs in VEX
	vbi.guest_stack_redzone_size = 0;
	vbi.guest_amd64_assume_fs_is_const = True;
	vbi.guest_amd64_assume_gs_is_const = True;

	//------------------------------------
	// options for instruction translation

	//
	// Architecture info
	//
	vta.arch_guest          = VexArch_INVALID; // to be assigned later
	vta.archinfo_host = vai_host;
#if __amd64__ || _WIN64
	vta.arch_host = VexArchAMD64;
#elif __i386__ || _WIN32
	vta.arch_host = VexArchX86;
#elif __arm__
	vta.arch_host = VexArchARM;
#elif __aarch64__
	vta.arch_host = VexArchARM64;
#else
#error "Unsupported host arch"
#endif

	//
	// The actual stuff to vex
	//
	vta.guest_bytes         = NULL;             // Set in vex_insts
	vta.guest_bytes_addr    = 0;                // Set in vex_insts

	//
	// callbacks
	//
	vta.callback_opaque     = NULL;             // Used by chase_into_ok, but never actually called
	vta.chase_into_ok       = chase_into_ok;    // Always returns false
	vta.preamble_function   = NULL;
	vta.instrument1         = instrument1;      // Callback we defined to help us save the IR
	vta.instrument2         = NULL;
	vta.finaltidy	    	= NULL;
	vta.needs_self_check	= needs_self_check;	

	#if 0
		vta.dispatch_assisted	= (void *)dispatch; // Not used
		vta.dispatch_unassisted	= (void *)dispatch; // Not used
	#else
		vta.disp_cp_chain_me_to_slowEP = (void *)dispatch; // Not used
		vta.disp_cp_chain_me_to_fastEP = (void *)dispatch; // Not used
		vta.disp_cp_xindir = (void *)dispatch; // Not used
		vta.disp_cp_xassisted = (void *)dispatch; // Not used
	#endif

	vta.guest_extents       = &vge;
	vta.host_bytes          = NULL;           // Buffer for storing the output binary
	vta.host_bytes_size     = 0;
	vta.host_bytes_used     = NULL;
	// doesn't exist? vta.do_self_check       = False;
	vta.traceflags          = 0;                // Debug verbosity
	//vta.traceflags          = -1;                // Debug verbosity
}

// Prepare the VexArchInfo struct
static void vex_prepare_vai(VexArch arch, VexArchInfo *vai)
{
	switch (arch)
	{
		case VexArchX86:
			vai->hwcaps =   VEX_HWCAPS_X86_MMXEXT |
							VEX_HWCAPS_X86_SSE1 |
							VEX_HWCAPS_X86_SSE2 |
							VEX_HWCAPS_X86_SSE3 |
							VEX_HWCAPS_X86_LZCNT;
			break;
		case VexArchAMD64:
			vai->hwcaps =   VEX_HWCAPS_AMD64_SSE3 |
							VEX_HWCAPS_AMD64_CX16 |
							VEX_HWCAPS_AMD64_LZCNT |
							VEX_HWCAPS_AMD64_AVX |
							VEX_HWCAPS_AMD64_RDTSCP |
							VEX_HWCAPS_AMD64_BMI |
							VEX_HWCAPS_AMD64_AVX2;
			break;
		case VexArchARM:
			vai->hwcaps = 7;
			break;
		case VexArchARM64:
			vai->hwcaps = 0;
			vai->arm64_dMinLine_lg2_szB = 6;
			vai->arm64_iMinLine_lg2_szB = 6;
			break;
		case VexArchPPC32:
			vai->hwcaps =   VEX_HWCAPS_PPC32_F |
							VEX_HWCAPS_PPC32_V |
							VEX_HWCAPS_PPC32_FX |
							VEX_HWCAPS_PPC32_GX |
							VEX_HWCAPS_PPC32_VX |
							VEX_HWCAPS_PPC32_DFP |
							VEX_HWCAPS_PPC32_ISA2_07;
			vai->ppc_icache_line_szB = 32; // unsure if correct
			break;
		case VexArchPPC64:
			vai->hwcaps =   VEX_HWCAPS_PPC64_V |
							VEX_HWCAPS_PPC64_FX |
							VEX_HWCAPS_PPC64_GX |
							VEX_HWCAPS_PPC64_VX |
							VEX_HWCAPS_PPC64_DFP |
							VEX_HWCAPS_PPC64_ISA2_07;
			vai->ppc_icache_line_szB = 64; // unsure if correct
			break;
		case VexArchS390X:
			vai->hwcaps = 0;
			break;
		case VexArchMIPS32:
			vai->hwcaps = 0x00010000;
			break;
		case VexArchMIPS64:
			vai->hwcaps = 0;
			break;
		default:
			pyvex_error("Invalid arch in vex_prepare_vai.\n");
			break;
	}
}

// Prepare the VexAbiInfo
static void vex_prepare_vbi(VexArch arch, VexAbiInfo *vbi)
{
	// only setting the guest_stack_redzone_size for now
	// this attribute is only specified by the PPC64 and AMD64 ABIs

	switch (arch)
	{
		case VexArchAMD64:
			vbi->guest_stack_redzone_size = 128;
			break;
		case VexArchPPC64:
			vbi->guest_stack_redzone_size = 288;
			break;
		default:
			break;
	}
}

//----------------------------------------------------------------------
// Translate 1 instruction to VEX IR.
//----------------------------------------------------------------------
static IRSB *vex_inst(VexArch guest, VexArchInfo archinfo, unsigned char *insn_start, unsigned long long insn_addr, int max_insns)
{
	vex_prepare_vai(guest, &archinfo);
	vex_prepare_vbi(guest, &vbi);

	debug("Guest arch: %d\n", guest);
	debug("Guest arch hwcaps: %08x\n", archinfo.hwcaps);
	//vta.traceflags = 0xffffffff;

	vta.archinfo_guest = archinfo;
	vta.arch_guest = guest;
	vta.abiinfo_both = vbi; // Set the vbi value

	vta.guest_bytes         = (UChar *)(insn_start);  // Ptr to actual bytes of start of instruction
	vta.guest_bytes_addr    = (Addr64)(insn_addr);

	debug("Setting VEX max instructions...\n");
	debug("... old: %d\n", vex_control.guest_max_insns);
	vex_control.guest_max_insns = max_insns;    // By default, we vex 1 instruction at a time
	debug("... new: %d\n", vex_control.guest_max_insns);

	// Do the actual translation
	vtr = LibVEX_Translate(&vta);
	debug("Translated!\n");

	assert(irbb_current);
	return irbb_current;
}

unsigned int vex_count_instructions(VexArch guest, VexArchInfo archinfo, unsigned char *instructions, unsigned long long block_addr, unsigned int num_bytes, int basic_only)
{
	debug("Counting instructions in %d bytes starting at 0x%x, basic %d\n", num_bytes, block_addr, basic_only);

	unsigned int count = 0;
	unsigned int processed = 0;
	int per_lift = basic_only ? 3 : 1;

	while (processed < num_bytes && count < 99)
	{
		debug("Next byte: %02x\n", instructions[processed]);
		IRSB *sb = vex_inst(guest, archinfo, instructions + processed, block_addr + processed, per_lift);

		if (vge.len[0] == 0 || sb == NULL)
		{
			if (sb) {
				// Block translated, got length of zero: first instruction is NoDecode
				count += 1;
			}
		pyvex_error("Something went wrong in IR translation at position %x of addr %x in vex_count_instructions.\n", processed, block_addr);
			break;
		}

		IRStmt *first_imark = NULL;
		IRStmt *first_exit = NULL;
		int bytes_until_exit = 0;
		int instrs_until_exit = 0;
		for (int i = 0; i < sb->stmts_used; i++) {
			if (sb->stmts[i]->tag == Ist_IMark) {
				if (first_imark == NULL) {
					first_imark = sb->stmts[i];
				}
				bytes_until_exit += sb->stmts[i]->Ist.IMark.len;
				instrs_until_exit += 1;
			}
			if (sb->stmts[i]->tag == Ist_Exit && sb->stmts[i]->Ist.Exit.jk == Ijk_Boring) {
				break;
			}
		}
		assert(first_imark);

		if (basic_only) {
			int guest_instrs = vtr.n_guest_instrs;
			int guest_bytes = vge.len[0];

			if (instrs_until_exit < guest_instrs) {
				guest_instrs = instrs_until_exit;
				guest_bytes = bytes_until_exit;
			}
			if (guest_instrs < 3) {
				// Found an exit!!
				if (processed + first_imark->Ist.IMark.len >= num_bytes) {
					// edge case: This is the first run through this loop (processed == 0) and
					// the first instruction is long enough to fill up the byte quota.
					count += 1;
					processed += first_imark->Ist.IMark.len;
					debug("Processed %d bytes\n", processed);
					break;
				}
				count += guest_instrs;
				processed += guest_bytes;
				debug("Processed %d bytes\n", processed);
				break;
			}
		}

		processed += first_imark->Ist.IMark.len;
		debug("Processed %d bytes\n", processed);

		assert(vge.n_used == 1);
		count++;
	}

	// count is one too high if the number of processed bytes is greater than num_bytes
	if (processed > num_bytes) {
	  count--;
	}

	debug("... found %d instructions!\n", count);
	return count;
}

IRSB *vex_block_bytes(VexArch guest, VexArchInfo archinfo, unsigned char *instructions, unsigned long long block_addr, unsigned int num_bytes, int basic_only)
{
	IRSB *sb = NULL;

	try
	{
		unsigned int count = vex_count_instructions(guest, archinfo, instructions, block_addr, num_bytes, basic_only);
		sb = vex_block_inst(guest, archinfo, instructions, block_addr, count);
		// this is a workaround. Basically, on MIPS, leaving this (the second translation of the same crap)
		// out leads to exits being dropped in some IRSBs
		sb = vex_block_inst(guest, archinfo, instructions, block_addr, count);
		if (vge.len[0] != num_bytes)
		{
			info("vex_block_bytes: only translated %d bytes out of %d in block_addr %x\n", vge.len[0], num_bytes, block_addr);
		}
		//assert(vge.len[0] == num_bytes);
	}
	catch (VEXError)
	{
		last_error = E4C_EXCEPTION.message;
	}

	return sb;
}

IRSB *vex_block_inst(VexArch guest, VexArchInfo archinfo, unsigned char *instructions, unsigned long long block_addr, unsigned int num_inst)
{
	debug("Translating %d instructions starting at 0x%x\n", num_inst, block_addr);

	if (num_inst == 0)
	{
		pyvex_error("vex_block_inst: asked to create IRSB with 0 instructions, at block_addr %x\n", block_addr);
		return NULL;
	}
	else if (num_inst > 99)
	{
		pyvex_error("vex_block_inst: maximum instruction count is 99.\n");
		num_inst = 99;
	}

	IRSB *fullblock = NULL;

	try
	{
		fullblock = vex_inst(guest, archinfo, instructions, block_addr, num_inst);
		assert(vge.n_used == 1);
	}
	catch (VEXError)
	{
		last_error = E4C_EXCEPTION.message;
	}

	return fullblock;
}

void set_iropt_level(int level) {
	vex_update_iropt_level(level);
}

void enable_debug(int debug) {
	debug_on = debug;
}
