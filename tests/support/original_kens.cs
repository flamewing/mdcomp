// A process boundary for the original 32-bit KENS 1.4 RC3 DLL API.
// Compile with the .NET Framework compiler: csc /platform:x86 /out:kens.exe ...
using System;
using System.Runtime.InteropServices;

class OriginalKens
{
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern IntPtr LoadLibrary(string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
    static extern IntPtr GetProcAddress(IntPtr library, string name);
    [DllImport("kernel32.dll")]
    static extern uint SetErrorMode(uint mode);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    delegate int Encode(string input, string output, [MarshalAs(UnmanagedType.I1)] bool option);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    delegate int Decode(string input, string output, int offset, [MarshalAs(UnmanagedType.I1)] bool option);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    delegate int NemEncode(string input, string output);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    delegate int NemDecode(string input, string output, int offset);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    delegate int SaxDecode(string input, string output, int offset, ushort size);

    static int Main(string[] args)
    {
        SetErrorMode(0x8003);
        if (args.Length != 5) {
            Console.Error.WriteLine("kens DLL-directory codec encode|decode input output");
            return 2;
        }
        try {
            string codec = args[1];
            bool encode = args[2] == "encode";
            if (!encode && args[2] != "decode") throw new ArgumentException("Unknown operation");
            if (codec != "Kosinski" && codec != "Enigma" && codec != "Nemesis" && codec != "Saxman")
                throw new ArgumentException("Unknown codec");
            IntPtr library = LoadLibrary(System.IO.Path.Combine(args[0], codec + ".dll"));
            if (library == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
            IntPtr symbol = GetProcAddress(library, (encode ? "Comp" : "Decomp") + (codec == "Saxman" ? "2" : ""));
            if (symbol == IntPtr.Zero) throw new ArgumentException("Missing DLL export");
            int result;
            if (codec == "Nemesis") {
                result = encode
                    ? ((NemEncode)Marshal.GetDelegateForFunctionPointer(symbol, typeof(NemEncode)))(args[3], args[4])
                    : ((NemDecode)Marshal.GetDelegateForFunctionPointer(symbol, typeof(NemDecode)))(args[3], args[4], 0);
            } else if (!encode && codec == "Saxman") {
                result = ((SaxDecode)Marshal.GetDelegateForFunctionPointer(symbol, typeof(SaxDecode)))(args[3], args[4], 0, 0);
            } else if (encode) {
                result = ((Encode)Marshal.GetDelegateForFunctionPointer(symbol, typeof(Encode)))(args[3], args[4], codec == "Saxman");
            } else {
                result = ((Decode)Marshal.GetDelegateForFunctionPointer(symbol, typeof(Decode)))(args[3], args[4], 0, false);
            }
            Console.WriteLine("KENS return value: " + result);
            return result == 0 ? 0 : 1;
        } catch (Exception error) {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
