// FindFunction.java — find the containing function for an arbitrary address.
//
// Invocation (headless):
//   analyzeHeadless <projectDir> <projectName> \
//       -process <programName> \
//       -readOnly \
//       -scriptPath <repo>/tools/ghidra-scripts \
//       -postScript FindFunction.java <addr_hex> [<addr_hex> ...]
//
// Output (one line per addr):
//   FN <addr> -> <namespace>::<name> @<entry>  size=<bytes>
// Used to convert call-site EIPs (from runtime instrumentation) back to
// the function entry that Decompile.java expects.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class FindFunction extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            println("FN_ERROR no addresses given");
            return;
        }
        for (String arg : args) {
            try {
                String hex = arg.toLowerCase().startsWith("0x")
                        ? arg.substring(2) : arg;
                long addrLong = Long.parseLong(hex, 16);
                Address a = currentProgram.getAddressFactory()
                        .getDefaultAddressSpace().getAddress(addrLong);
                Function fn = currentProgram.getFunctionManager()
                        .getFunctionContaining(a);
                if (fn == null) {
                    println("FN " + arg + " -> (none)");
                    continue;
                }
                String ns = fn.getParentNamespace() != null
                        ? fn.getParentNamespace().getName() : "Global";
                println(String.format("FN %s -> %s::%s @%s  size=%d",
                        arg, ns, fn.getName(),
                        fn.getEntryPoint().toString(),
                        fn.getBody().getNumAddresses()));
            } catch (Exception e) {
                println("FN_ERROR " + arg + " -> " + e.getMessage());
            }
        }
    }
}
