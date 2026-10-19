using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;

// Standalone invocation fixture, compiled only into the test's temporary folder.
public static class BC2RenderObserver
{
    public static int Main(string[] args)
    {
        if (args.Length != 3 || args[0] != "--game" || args[2] != "--render-only")
        {
            Console.Error.WriteLine("Incorrect fixture arguments");
            return 10;
        }
        string folder = args[1];
        string countPath = Path.Combine(folder, "count.txt");
        int count = File.Exists(countPath) ? int.Parse(File.ReadAllText(countPath), CultureInfo.InvariantCulture) : 0;
        File.WriteAllText(countPath, (count + 1).ToString(CultureInfo.InvariantCulture));
        File.WriteAllText(Path.Combine(folder, "child-pid.txt"), Process.GetCurrentProcess().Id.ToString(CultureInfo.InvariantCulture));
        string[] modes = File.ReadAllText(Path.Combine(folder, "mode.txt")).Trim().Split(',');
        string mode = modes[Math.Min(count, modes.Length - 1)];
        int pid = int.Parse(File.ReadAllText(Path.Combine(folder, "pid.txt")), CultureInfo.InvariantCulture);
        if (mode == "hang") { Thread.Sleep(60000); return 0; }
        if (mode == "transient" || mode == "bigtransient")
        {
            Console.Error.WriteLine("Read-only BC2 observation failed: Renderer not initialized yet");
            if (mode == "bigtransient")
                for (int i = 0; i < 4096; ++i) Console.Error.WriteLine(new string('x', 128));
            return 1;
        }
        if (mode == "fatal")
        {
            Console.Error.WriteLine("Read-only BC2 observation failed: Live native code differs from the verified discovery relationships");
            return 1;
        }
        if (mode == "unsupported")
        {
            Console.WriteLine("{\"schema\":1,\"state\":\"unsupported_discovery_profile\",\"native_capabilities\":0}");
            return 2;
        }
        if (mode == "malformed") { Console.WriteLine("{invalid-json"); return 0; }
        if (mode == "wrongpid") ++pid;
        int width = mode == "mismatch" ? 1024 : 1920;
        int height = mode == "mismatch" ? 768 : 1080;
        Console.WriteLine("{\"schema\":1,\"state\":\"renderer_observed\",\"pid\":" + pid +
            ",\"graphics_api\":\"D3D11\",\"width\":" + width + ",\"height\":" + height +
            ",\"stable\":true,\"pending_reset\":false}");
        return 0;
    }
}
