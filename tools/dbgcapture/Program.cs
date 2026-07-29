// dbgcapture — captures Win32 OutputDebugStringA broadcasts to a file.
//
// Windows ships a global broadcast for OutputDebugStringA: a named shared-memory
// section "DBWIN_BUFFER" plus two named auto-reset events,
// "DBWIN_BUFFER_READY" (writers wait on this; we set it when the buffer is free)
// and "DBWIN_DATA_READY" (writers signal this after writing; we wait on it).
// The buffer layout is: 4-byte ProcessId (DWORD) + null-terminated ANSI string.
//
// This program creates/opens those objects, drains every broadcast, and writes
// each message to a log file with a UTC timestamp and source PID.
//
// Usage:
//   dbgcapture                     -> writes ./dbgcapture.log
//   dbgcapture <path>              -> writes <path>
//   dbgcapture <path> --quiet      -> file only, no console echo
//
// Run BEFORE launching the process whose output you want to capture. If the
// shared section doesn't exist when a process calls OutputDebugStringA, the
// call silently does nothing — broadcasts are lost.

using System.IO.MemoryMappedFiles;
using System.Text;

const string BufferReadyName = "DBWIN_BUFFER_READY";
const string DataReadyName   = "DBWIN_DATA_READY";
const string BufferName      = "DBWIN_BUFFER";
const int    BufferSize      = 4096;

string logPath = args.Length > 0 ? args[0] : "dbgcapture.log";
bool quiet = args.Contains("--quiet");

string fullLogPath = Path.GetFullPath(logPath);
string? logDir = Path.GetDirectoryName(fullLogPath);
if (!string.IsNullOrEmpty(logDir)) Directory.CreateDirectory(logDir);

EventWaitHandle bufferReady;
EventWaitHandle dataReady;
MemoryMappedFile mmf;

try
{
    bufferReady = new EventWaitHandle(false, EventResetMode.AutoReset, BufferReadyName, out _);
    dataReady   = new EventWaitHandle(false, EventResetMode.AutoReset, DataReadyName,   out _);
    mmf = MemoryMappedFile.CreateOrOpen(BufferName, BufferSize, MemoryMappedFileAccess.ReadWrite);
}
catch (Exception ex)
{
    Console.Error.WriteLine($"ERROR: failed to open debug-string broadcast objects: {ex.Message}");
    Console.Error.WriteLine("Another capture tool (e.g. DebugView) may already hold them.");
    return 1;
}

using var accessor = mmf.CreateViewAccessor(0, BufferSize, MemoryMappedFileAccess.ReadWrite);
bufferReady.Set();  // signal writers that the buffer is free

var cancel = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) =>
{
    e.Cancel = true;
    cancel.Cancel();
};

Console.WriteLine($"dbgcapture: listening for OutputDebugString -> {fullLogPath}");
Console.WriteLine("Press Ctrl+C to stop.");

using var writer = new StreamWriter(fullLogPath, append: true) { AutoFlush = true };
writer.WriteLine($"[{IsoNow()}] === dbgcapture session start (pid={Environment.ProcessId}) ===");

byte[] data = new byte[BufferSize - 4];

while (!cancel.IsCancellationRequested)
{
    if (!dataReady.WaitOne(TimeSpan.FromMilliseconds(250))) continue;

    uint pid = accessor.ReadUInt32(0);
    accessor.ReadArray(4, data, 0, data.Length);

    int len = Array.IndexOf(data, (byte)0);
    if (len < 0) len = data.Length;
    string msg = Encoding.Default.GetString(data, 0, len).TrimEnd('\r', '\n');

    string line = $"[{IsoNow()}] [pid={pid,5}] {msg}";
    writer.WriteLine(line);
    if (!quiet) Console.WriteLine(line);

    bufferReady.Set();  // release the buffer for the next writer
}

writer.WriteLine($"[{IsoNow()}] === dbgcapture session end ===");
Console.WriteLine("dbgcapture: stopped.");
return 0;

static string IsoNow() => DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ");
