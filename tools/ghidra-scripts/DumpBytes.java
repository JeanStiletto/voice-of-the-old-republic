// DumpBytes.java — print N bytes at given addresses, one line per address.
//
// Invocation (headless):
//   analyzeHeadless <projectDir> <projectName> \
//       -process <programName> \
//       -readOnly \
//       -scriptPath <repo>/tools/ghidra-scripts \
//       -postScript DumpBytes.java <addr_hex>:<count> [<addr_hex>:<count> ...]
//
// Each argument is "ADDR:COUNT" where ADDR is hex (with or without 0x) and
// COUNT is decimal. Default count is 16. Output format (one line per addr):
//   BYTES <addr> = <hex bytes space-separated>
// We use a "BYTES " prefix so callers can grep our output out of Ghidra's
// noisy startup banner.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class DumpBytes extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            println("BYTES_ERROR no addresses given (usage: DumpBytes <hex>:<count> ...)");
            return;
        }
        for (String arg : args) {
            try {
                String[] parts = arg.split(":");
                String hex = parts[0].toLowerCase().startsWith("0x")
                        ? parts[0].substring(2) : parts[0];
                long addrLong = Long.parseLong(hex, 16);
                int count = parts.length > 1 ? Integer.parseInt(parts[1]) : 16;
                Address a = currentProgram.getAddressFactory()
                        .getDefaultAddressSpace().getAddress(addrLong);
                byte[] bytes = new byte[count];
                currentProgram.getMemory().getBytes(a, bytes);
                StringBuilder sb = new StringBuilder();
                sb.append(String.format("BYTES 0x%08x =", addrLong));
                for (byte b : bytes) sb.append(String.format(" %02x", b & 0xff));
                println(sb.toString());
            } catch (Exception e) {
                println("BYTES_ERROR " + arg + " -> " + e.getMessage());
            }
        }
    }
}
