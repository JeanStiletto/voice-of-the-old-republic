// ListSymbolsByName.java — list ALL symbols (functions + data) whose name
// matches a substring. Useful for finding labelled global variables like
// `cameraPersonalSpace` that aren't functions.
//
// Invocation:
//   analyzeHeadless ... -postScript ListSymbolsByName.java <substring> [...]
//
// Output: "SYM <addr> <namespace>::<name> [type]"

import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class ListSymbolsByName extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            println("LSN_ERROR no substrings given");
            return;
        }
        SymbolTable st = currentProgram.getSymbolTable();
        SymbolIterator iter = st.getAllSymbols(true);
        while (iter.hasNext()) {
            Symbol s = iter.next();
            String nm = s.getName();
            String ns = s.getParentNamespace() != null
                    ? s.getParentNamespace().getName() : "Global";
            for (String arg : args) {
                if (nm.toLowerCase().contains(arg.toLowerCase())) {
                    println(String.format("SYM %s %s::%s [%s]",
                            s.getAddress().toString(), ns, nm,
                            s.getSymbolType().toString()));
                    break;
                }
            }
        }
    }
}
