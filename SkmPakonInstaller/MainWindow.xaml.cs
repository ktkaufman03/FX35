using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using MahApps.Metro.Controls;
using Microsoft.Win32;
using Path = System.IO.Path;

namespace SkmPakonInstaller
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : MetroWindow
    {
        private string _comServerPath;
        private const string InstallationFilesDirectory = "InstallationFiles";

        public MainWindow()
        {
            InitializeComponent();

            using var klm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry32);
            using var tlxKey = klm.OpenSubKey(@"SOFTWARE\Pakon\TLX");

            if (tlxKey == null)
            {
                MessageBox.Show(@"Could not find HKLM\SOFTWARE\Pakon\TLX in registry.", "Registry Lookup Error", MessageBoxButton.OK, MessageBoxImage.Error);
                Environment.Exit(1);
                return;
            }

            var programPathValue = tlxKey.GetValue("ProgramPath");

            if (programPathValue is not string programPath)
            {
                MessageBox.Show("Could not find ProgramPath string in TLX registry key.", "Registry Lookup Error", MessageBoxButton.OK, MessageBoxImage.Error);
                Environment.Exit(1);
                return;
            }

            if (!Directory.Exists(programPath))
            {
                MessageBox.Show($"COM server appears to be missing (wanted to find folder: {programPath})", "Registry Lookup Error", MessageBoxButton.OK, MessageBoxImage.Error);
                Environment.Exit(1);
                return;
            }

            PakonSoftwareFolderTextBox.Text = _comServerPath = programPath;

            if (!Directory.Exists(InstallationFilesDirectory))
            {
                MessageBox.Show($"Could not find installation files directory: {InstallationFilesDirectory}",
                    "Installer Error", MessageBoxButton.OK, MessageBoxImage.Error);
                Environment.Exit(1);
                return;
            }
        }

        private void InstallButton_OnClick(object sender, RoutedEventArgs e)
        {
            InstallButton.IsEnabled = false;
            var tlxDllNames = new[] { "TLA", "TLB", "TLC" };

            foreach (string tlxDllName in tlxDllNames)
            {
                var origPath = Path.Combine(_comServerPath, $"{tlxDllName}.dll");

                if (File.Exists(origPath)) continue;

                MessageBox.Show($"Could not find module: {tlxDllName} (expected to find it at: {origPath})", "Installer Error", MessageBoxButton.OK,
                    MessageBoxImage.Error);
                Environment.Exit(1);
                return;
            }

            var psi = new ProcessStartInfo();
            psi.FileName = "pnputil";
            psi.WorkingDirectory = Path.Combine(Environment.CurrentDirectory, InstallationFilesDirectory);
            psi.ArgumentList.Add("/add-driver");
            psi.ArgumentList.Add("*.inf");
            psi.ArgumentList.Add("/install");
            psi.ArgumentList.Add("/subdirs");
            psi.Verb = "runas";
            psi.RedirectStandardOutput = true;
            psi.RedirectStandardError = true;
            var proc = Process.Start(psi);

            if (proc == null)
            {
                MessageBox.Show("Could not start PnPUtil to install drivers.", "Installer Error", MessageBoxButton.OK,
                    MessageBoxImage.Error);
                return;
            }

            //proc.EnableRaisingEvents = true;
            proc.WaitForExit();

            if (proc.ExitCode != 0)
            {
                var logfilename = $"drv_install_log_{DateTimeOffset.Now.ToUnixTimeSeconds()}.txt";
                using var log = new StreamWriter(File.Create(logfilename));
                log.WriteLine("StdOut:");
                log.Write(proc.StandardOutput.ReadToEnd());
                log.WriteLine();
                log.WriteLine("StdErr:");
                log.Write(proc.StandardError.ReadToEnd());
                log.WriteLine();

                MessageBox.Show($"PnPUtil exited with non-zero status: {proc.ExitCode} - see {logfilename} for more info", "Installer Error", MessageBoxButton.OK,
                    MessageBoxImage.Error);

                return;
            }

            foreach (string tlxDllName in tlxDllNames)
            {
                var origPath = Path.Combine(_comServerPath, $"{tlxDllName}.dll");
                var backupPath = origPath + ".bak";
                if (!File.Exists(backupPath))
                    File.Move(origPath, backupPath);
                File.Copy(Path.Combine(InstallationFilesDirectory, $"{tlxDllName}.dll"), origPath, true);
            }

            MessageBox.Show("Done!", "Installer Message", MessageBoxButton.OK, MessageBoxImage.Information);
            Environment.Exit(0);
            //File.Copy();
        }
    }
}
