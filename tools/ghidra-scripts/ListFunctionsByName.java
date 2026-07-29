// ListFunctionsByName.java — list all functions whose name (or fully-qualified
// name) matches a substring. Used to enumerate sibling functions on a class
// like CSWCModule when only one Accl* address is known.
//
// Invocation (headless):
//   analyzeHeadless <projectDir> <projectName> \
//       -process <programName> \
//       -readOnly \
//       -scriptPath <repo>/tools/ghidra-scripts \
//       -postScript ListFunctionsByName.java <substring> [<substring> ...]
//
// Output: one line per match: "FN <namespace>::<name> @<addr> size=<bytes>"

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ListFunctionsByName extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            println("LFN_ERROR no substrings given");
            return;
        }
        FunctionIterator iter = currentProgram.getFunctionManager().getFunctions(true);
        while (iter.hasNext()) {
            Function fn = iter.next();
            String ns = fn.getParentNamespace() != null
                    ? fn.getParentNamespace().getName() : "Global";
            String nm = fn.getName();
            String full = ns + "::" + nm;
            for (String arg : args) {
                if (full.toLowerCase().contains(arg.toLowerCase())
                        || nm.toLowerCase().contains(arg.toLowerCase())) {
                    println(String.format("FN %s::%s @%s size=%d",
                            ns, nm, fn.getEntryPoint().toString(),
                            fn.getBody().getNumAddresses()));
                    break;
                }
            }
        }
    }
}
