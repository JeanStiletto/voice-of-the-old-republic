// PrintListing.java — print the disassembly listing (address + mnemonic +
// operands) for the function containing each given address, or for an explicit
// ADDR:COUNT instruction window. Use when the decompiler hides the numeric
// struct offset you need (e.g. a `CMP byte ptr [reg+0xNN], imm` field probe)
// behind a symbolic field name.
//
// Invocation (headless):
//   analyzeHeadless <projectDir> <projectName> \
//       -process <programName> \
//       -readOnly \
//       -scriptPath <repo>/tools/ghidra-scripts \
//       -postScript PrintListing.java <addr_hex>[:<count>] [<addr_hex>[:<count>] ...]
//
// Without :count, prints the whole containing function. With :count, prints
// that many instructions starting at the address. Output lines are prefixed
// "LST " so callers can grep them out of Ghidra's startup banner.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

public class PrintListing extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            println("LST_ERROR no addresses given (usage: PrintListing <hex>[:<count>] ...)");
            return;
        }
        Listing listing = currentProgram.getListing();
        for (String arg : args) {
            String[] parts = arg.split(":");
            String hex = parts[0].toLowerCase().startsWith("0x")
                    ? parts[0].substring(2) : parts[0];
            Address start = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(Long.parseLong(hex, 16));
            Integer count = parts.length > 1 ? Integer.parseInt(parts[1]) : null;

            Address end = null;
            if (count == null) {
                Function fn = listing.getFunctionContaining(start);
                if (fn == null) {
                    println("LST_ERROR no function at " + arg);
                    continue;
                }
                start = fn.getEntryPoint();
                end = fn.getBody().getMaxAddress();
                println(String.format("===== LISTING %s @%s =====",
                        fn.getName(), start));
            }

            Instruction insn = listing.getInstructionAt(start);
            int printed = 0;
            while (insn != null) {
                println(String.format("LST %s  %s", insn.getAddress(), insn.toString()));
                printed++;
                if (count != null && printed >= count) break;
                insn = insn.getNext();
                if (end != null && insn != null
                        && insn.getAddress().compareTo(end) > 0) break;
            }
        }
    }
}
