// Decompile.java — emit Ghidra decompiled C-like pseudocode for one or more
// functions, identified by entry-point address.
//
// Invocation (headless):
//   analyzeHeadless <projectDir> <projectName> \
//       -process <programName> \
//       -readOnly \
//       -scriptPath <repo>/tools/ghidra-scripts \
//       -postScript Decompile.java [<outFile>] <addr_hex> [<addr_hex> ...]
//
// If the FIRST arg is a path (contains '/' or '\', or isn't hex-parseable), the
// decompiled C is written CLEANLY to that file — no Ghidra log prefixes, no
// doubled lines, no startup banner — so the caller can just read the file. This
// is the smooth path (see tools/ghidra-scripts/decomp.sh). Without an outFile it
// falls back to println(), which the headless logger mirrors+prefixes (noisy).
//
// Output: each function delimited by "===== DECOMP <namespace>::<name> @<addr> =====".

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class Decompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            println("DECOMP_ERROR no addresses given");
            return;
        }

        // First arg is an output file if it looks like a path or isn't hex.
        String outFile = null;
        List<String> addrs = new ArrayList<>();
        for (String arg : args) {
            if (outFile == null && addrs.isEmpty() && looksLikePath(arg)) {
                outFile = arg;
            } else {
                addrs.add(arg);
            }
        }

        StringBuilder out = new StringBuilder();
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        if (!decomp.openProgram(currentProgram)) {
            emit(out, outFile, "DECOMP_ERROR could not open program for decompiler");
            flush(out, outFile);
            return;
        }
        try {
            for (String arg : addrs) {
                try {
                    String hex = arg.toLowerCase().startsWith("0x")
                            ? arg.substring(2) : arg;
                    long addrLong = Long.parseLong(hex, 16);
                    Address a = currentProgram.getAddressFactory()
                            .getDefaultAddressSpace().getAddress(addrLong);
                    Function fn = currentProgram.getFunctionManager().getFunctionAt(a);
                    if (fn == null) {
                        emit(out, outFile, "DECOMP_ERROR no function at " + arg);
                        continue;
                    }
                    String ns = fn.getParentNamespace() != null
                            ? fn.getParentNamespace().getName() : "Global";
                    emit(out, outFile, String.format("===== DECOMP %s::%s @%s =====",
                            ns, fn.getName(), a.toString()));
                    DecompileResults res = decomp.decompileFunction(fn, 60, new ConsoleTaskMonitor());
                    if (res != null && res.decompileCompleted()) {
                        emit(out, outFile, res.getDecompiledFunction().getC());
                    } else {
                        emit(out, outFile, "DECOMP_ERROR decompilation failed for " + arg);
                    }
                } catch (Exception e) {
                    emit(out, outFile, "DECOMP_ERROR " + arg + " -> " + e.getMessage());
                }
            }
        } finally {
            decomp.dispose();
        }
        flush(out, outFile);
    }

    // True if arg is meant as an output path rather than an address.
    private boolean looksLikePath(String arg) {
        if (arg.indexOf('/') >= 0 || arg.indexOf('\\') >= 0) return true;
        String hex = arg.toLowerCase().startsWith("0x") ? arg.substring(2) : arg;
        try { Long.parseLong(hex, 16); return false; } catch (Exception e) { return true; }
    }

    // Accumulate to the file buffer; mirror to println only in non-file mode.
    private void emit(StringBuilder out, String outFile, String s) {
        if (outFile != null) { out.append(s).append('\n'); } else { println(s); }
    }

    private void flush(StringBuilder out, String outFile) {
        if (outFile == null) return;
        try (BufferedWriter w = new BufferedWriter(new FileWriter(outFile))) {
            w.write(out.toString());
            println("DECOMP_DONE wrote " + outFile);
        } catch (IOException e) {
            println("DECOMP_ERROR could not write " + outFile + " -> " + e.getMessage());
        }
    }
}
