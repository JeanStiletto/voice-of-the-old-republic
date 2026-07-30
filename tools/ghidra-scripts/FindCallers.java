// FindCallers.java — list every call site referencing one or more
// target addresses. For each call site prints the caller function
// (namespace::name @addr) and the call-instruction address.
//
// Invocation (headless):
//   analyzeHeadless <projectDir> <projectName> \
//       -process <programName> -readOnly \
//       -scriptPath <repo>/tools/ghidra-scripts \
//       -postScript FindCallers.java <addr_hex> [<addr_hex> ...]
//
// Output lines start with "CALLER " so they can be grepped out of
// the Ghidra startup banner.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.RefType;

public class FindCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            println("FINDCALLERS_ERROR no addresses given");
            return;
        }

        for (String arg : args) {
            String hex = arg.toLowerCase().startsWith("0x")
                    ? arg.substring(2) : arg;
            long addrLong = Long.parseLong(hex, 16);
            Address target = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(addrLong);

            Function targetFn = currentProgram.getFunctionManager()
                    .getFunctionAt(target);
            String targetLabel = targetFn != null
                    ? (targetFn.getParentNamespace().getName()
                       + "::" + targetFn.getName())
                    : "<unknown>";
            println(String.format("===== CALLERS OF %s @%s =====",
                    targetLabel, target.toString()));

            ReferenceIterator refs = currentProgram
                    .getReferenceManager().getReferencesTo(target);
            int count = 0;
            while (refs.hasNext()) {
                Reference ref = refs.next();
                RefType rt = ref.getReferenceType();
                if (!rt.isCall() && !rt.isJump()) continue;
                Address from = ref.getFromAddress();
                Function caller = currentProgram.getFunctionManager()
                        .getFunctionContaining(from);
                String callerLabel = caller != null
                        ? (caller.getParentNamespace().getName()
                           + "::" + caller.getName()
                           + " @" + caller.getEntryPoint().toString())
                        : "<no-function>";
                println(String.format("CALLER %s call-at=%s type=%s",
                        callerLabel, from.toString(), rt.getName()));
                ++count;
            }
            println(String.format("===== END CALLERS — %d found =====",
                    count));
        }
    }
}
