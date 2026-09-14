// FindScalar.java — list every instruction carrying a given immediate
// (scalar operand), grouped by containing function. The headless way to
// answer "which function allocates 0x1400 bytes" or "who passes this magic
// constant" when the binary has no symbol names to search (KOTOR 2).
//
// Invocation (headless):
//   analyzeHeadless <projectDir> <projectName> \
//       -process <programName> -readOnly \
//       -scriptPath <repo>/tools/ghidra-scripts \
//       -postScript FindScalar.java <value_hex> [<value_hex> ...]
//
// Output lines start with "SCALAR " so they can be grepped out of the
// Ghidra startup banner. Only .text-resident instructions are scanned;
// a value that also appears as data (vtables, tables) is not reported.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

public class FindScalar extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            println("FINDSCALAR_ERROR no values given");
            return;
        }
        long[] wanted = new long[args.length];
        for (int i = 0; i < args.length; ++i) {
            String hex = args[i].toLowerCase().startsWith("0x")
                    ? args[i].substring(2) : args[i];
            wanted[i] = Long.parseLong(hex, 16);
        }

        for (long value : wanted) {
            println(String.format("===== SCALAR 0x%x =====", value));
            int count = 0;
            InstructionIterator it = currentProgram.getListing()
                    .getInstructions(true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                boolean hit = false;
                for (int op = 0; op < ins.getNumOperands() && !hit; ++op) {
                    for (Object o : ins.getOpObjects(op)) {
                        if (o instanceof Scalar
                                && ((Scalar) o).getUnsignedValue() == value) {
                            hit = true;
                            break;
                        }
                    }
                }
                if (!hit) continue;
                Address at = ins.getAddress();
                Function fn = currentProgram.getFunctionManager()
                        .getFunctionContaining(at);
                String fnLabel = fn != null
                        ? (fn.getParentNamespace().getName() + "::"
                           + fn.getName() + " @" + fn.getEntryPoint())
                        : "<no-function>";
                println(String.format("SCALAR 0x%x in %s at=%s ins=%s",
                        value, fnLabel, at.toString(), ins.toString()));
                ++count;
            }
            println(String.format("===== END SCALAR — %d found =====", count));
        }
    }
}
