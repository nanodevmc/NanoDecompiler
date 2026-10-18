using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using AvaloniaEdit.Highlighting;

namespace NanoDecompilerLite;

public partial class MainWindow : Window
{
    private string? _currentOutDir;
    private readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(6) };
    private const string CurrentVersion = "2.0.0-alpha";

    public MainWindow()
    {
        InitializeComponent();

        OpenJarButton.Click += OnOpenJarClicked;
        CheckUpdateButton.Click += OnCheckUpdateClicked;
        CopyCodeButton.Click += OnCopyCodeClicked;
        FileTreeView.SelectionChanged += OnTreeSelectionChanged;
        SearchBox.TextChanged += (s, e) => ApplyFilter();

        // Настройка синтаксической подсветки C# / Java в AvaloniaEdit
        CodeEditor.SyntaxHighlighting = HighlightingManager.Instance.GetDefinition("C#") ??
                                        HighlightingManager.Instance.GetDefinition("Java");

        // Результаты прошлых запусков копились в профиле навсегда - подчищаем старые при старте
        // и текущий при закрытии окна; HttpClient освобождаем (раньше не освобождался).
        CleanupStaleOutputs();
        Closed += (s, e) =>
        {
            DeleteOutputDir(_currentOutDir);
            _http.Dispose();
        };

        // Фоновая проверка обновления при старте
        _ = CheckForUpdatesAsync(silent: true);
    }

    private async void OnOpenJarClicked(object? sender, RoutedEventArgs e)
    {
        try
        {
            await OpenJarAsync();
        }
        catch (Exception ex)
        {
            // async void: необработанное исключение здесь завершало приложение целиком.
            StatusLabel.Text = $"Ошибка: {ex.Message}";
            OpenJarButton.IsEnabled = true;
        }
    }

    private async Task OpenJarAsync()
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Выберите JAR или архив для декомпиляции",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("Java Archive (*.jar)") { Patterns = new[] { "*.jar", "*.zip", "*.7z", "*.tar.gz" } },
                new FilePickerFileType("Все файлы") { Patterns = new[] { "*.*" } }
            }
        });

        if (files.Count == 0) return;

        string jarPath = files[0].Path.LocalPath;
        await DecompileJarAsync(jarPath);
    }

    // Каталог результатов - в профиле ТЕКУЩЕГО пользователя (раньше общий %TEMP%/NanoDecompilerLite:
    // предсказуемое имя в общей папке, куда может писать любой процесс).
    private static string AppDataRoot()
    {
        string baseDir = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrEmpty(baseDir)) baseDir = Path.GetTempPath();
        return Path.Combine(baseDir, "NanoDecompilerLite");
    }

    private static string OutputRoot() => Path.Combine(AppDataRoot(), "out");

    private static string SafeName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var cleaned = new string(name.Select(c => invalid.Contains(c) ? '_' : c).ToArray()).Trim();
        if (cleaned.Length == 0) cleaned = "plugin";
        return cleaned.Length > 60 ? cleaned[..60] : cleaned;
    }

    private static void DeleteOutputDir(string? dir)
    {
        if (string.IsNullOrEmpty(dir)) return;
        try
        {
            // Удаляем только внутри нашей папки результатов.
            string root = Path.GetFullPath(OutputRoot()) + Path.DirectorySeparatorChar;
            string full = Path.GetFullPath(dir);
            if (full.StartsWith(root, StringComparison.OrdinalIgnoreCase) && Directory.Exists(full))
                Directory.Delete(full, recursive: true);
        }
        catch { }
    }

    // Результаты прошлых запусков (в т.ч. после аварийного закрытия) копились навсегда.
    private static void CleanupStaleOutputs()
    {
        try
        {
            string root = OutputRoot();
            if (!Directory.Exists(root)) return;
            foreach (var d in new DirectoryInfo(root).GetDirectories())
            {
                if (DateTime.UtcNow - d.LastWriteTimeUtc > TimeSpan.FromHours(24)) DeleteOutputDir(d.FullName);
            }
        }
        catch { }
    }

    // Ошибка из JSON-ответа движка (--json-output): {"status":"error","error":"..."}.
    private static string? ParseEngineError(string stdout)
    {
        foreach (var raw in stdout.Split('\n').Reverse())
        {
            var line = raw.Trim();
            if (!line.StartsWith("{")) continue;
            try
            {
                using var doc = JsonDocument.Parse(line);
                if (doc.RootElement.TryGetProperty("status", out var st) && st.GetString() == "error")
                    return doc.RootElement.TryGetProperty("error", out var er) ? er.GetString() ?? "неизвестная ошибка" : "неизвестная ошибка";
                return null;
            }
            catch (JsonException) { }
        }
        return null;
    }

    private async Task DecompileJarAsync(string jarPath)
    {
        // Результат прошлого запуска больше не нужен.
        DeleteOutputDir(_currentOutDir);
        _currentOutDir = null;

        string baseTemp = OutputRoot();
        Directory.CreateDirectory(baseTemp);
        string outDir = Path.Combine(baseTemp, SafeName(Path.GetFileNameWithoutExtension(jarPath)) + "_" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(outDir);

        StatusLabel.Text = $"Декомпиляция {Path.GetFileName(jarPath)} через движок C++...";
        OpenJarButton.IsEnabled = false;
        bool success = false;

        try
        {
            string cliPath = FindCliExecutable();
            if (string.IsNullOrEmpty(cliPath) || !File.Exists(cliPath))
            {
                StatusLabel.Text = "CLI движок не найден автоматически. Выберите файл движка...";
                var picked = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
                {
                    Title = "Укажите NanoDecompilerCLI.exe / NanoDecompilerClApi.exe",
                    AllowMultiple = false,
                    FileTypeFilter = new List<FilePickerFileType>
                    {
                        new FilePickerFileType("NanoDecompiler CLI Engine") { Patterns = new[] { "NanoDecompilerCLI*.exe", "NanoDecompilerClApi*.exe", "NanoDecompilerCLI*", "*.exe" } },
                        new FilePickerFileType("Все файлы") { Patterns = new[] { "*.*" } }
                    }
                });

                if (picked.Count > 0 && File.Exists(picked[0].Path.LocalPath))
                {
                    _manualCliPath = picked[0].Path.LocalPath;
                    cliPath = _manualCliPath;
                }
                else
                {
                    StatusLabel.Text = "Ошибка: CLI движок не найден. Установите полный клиент или поместите NanoDecompilerCLI.exe рядом.";
                    return;
                }
            }

            var psi = new ProcessStartInfo
            {
                FileName = cliPath,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };
            // ArgumentList вместо строки с ручными кавычками: путь с завершающим "\" или кавычкой
            // раньше ломал разбор командной строки.
            psi.ArgumentList.Add(jarPath);
            psi.ArgumentList.Add(outDir);
            psi.ArgumentList.Add("--no-legitimacy-check");
            psi.ArgumentList.Add("--json-output");

            var sw = Stopwatch.StartNew();
            using var proc = Process.Start(psi);
            if (proc == null) throw new Exception("Не удалось запустить процесс движка.");

            // stdout И stderr читаются параллельно: раньше stderr был перенаправлен, но не читался -
            // при большом выводе в stderr движок блокировался на полном буфере (взаимная блокировка).
            var stdoutTask = proc.StandardOutput.ReadToEndAsync();
            var stderrTask = proc.StandardError.ReadToEndAsync();
            using (var cts = new CancellationTokenSource(TimeSpan.FromMinutes(15)))
            {
                try
                {
                    await proc.WaitForExitAsync(cts.Token);
                }
                catch (OperationCanceledException)
                {
                    try { proc.Kill(entireProcessTree: true); } catch { }
                    throw new Exception("Движок не завершился за 15 минут.");
                }
            }
            string stdout = await stdoutTask;
            string stderr = await stderrTask;
            sw.Stop();

            // Раньше код возврата и JSON-ошибка движка игнорировались: интерфейс всегда писал "Успешно".
            string? engineError = ParseEngineError(stdout);
            if (proc.ExitCode != 0 || engineError != null)
            {
                string details = engineError ?? (string.IsNullOrWhiteSpace(stderr) ? $"код возврата {proc.ExitCode}" : stderr.Trim().Split('\n').Last());
                StatusLabel.Text = $"Ошибка декомпиляции: {details}";
                return;
            }

            _currentOutDir = outDir;
            PopulateFileTree(outDir);
            success = true;
            StatusLabel.Text = $"Успешно декомпилировано за {sw.ElapsedMilliseconds} мс. Найдено классов: {Directory.GetFiles(outDir, "*.java", SearchOption.AllDirectories).Length}";
        }
        catch (Exception ex)
        {
            StatusLabel.Text = $"Ошибка декомпиляции: {ex.Message}";
        }
        finally
        {
            if (!success) DeleteOutputDir(outDir);
            OpenJarButton.IsEnabled = true;
        }
    }

    private string? _manualCliPath;

    private static string FileSha256Hex(string path)
    {
        using var fs = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(fs)).ToLowerInvariant();
    }

    // Встроенный движок распаковывается в каталог ТЕКУЩЕГО пользователя, имя подкаталога -
    // хеш содержимого. Раньше путь был общим предсказуемым (%TEMP%/NanoDecompilerLite/embedded_engine),
    // а "актуальность" проверялась только по РАЗМЕРУ файла: подложенный заранее exe такой же
    // длины запускался вместо настоящего движка.
    private string ExtractEmbeddedCliIfAvailable()
    {
        try
        {
            string binName = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "NanoDecompilerCLI.exe" : "NanoDecompilerCLI";
            var asm = Assembly.GetExecutingAssembly();
            string[] resNames = asm.GetManifestResourceNames();
            string? match = resNames.FirstOrDefault(n =>
                n.EndsWith(binName, StringComparison.OrdinalIgnoreCase) ||
                n.EndsWith("NanoDecompilerCLI.exe", StringComparison.OrdinalIgnoreCase) ||
                n.EndsWith("NanoDecompilerCLI", StringComparison.OrdinalIgnoreCase));
            if (string.IsNullOrEmpty(match)) return "";

            using var stream = asm.GetManifestResourceStream(match);
            if (stream == null) return "";
            using var ms = new MemoryStream();
            stream.CopyTo(ms);
            byte[] data = ms.ToArray();
            string hash = Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant();

            string targetDir = Path.Combine(AppDataRoot(), "engine", hash[..16]);
            Directory.CreateDirectory(targetDir);
            string targetPath = Path.Combine(targetDir, binName);

            if (File.Exists(targetPath) && FileSha256Hex(targetPath) == hash) return targetPath;

            // Пишем во временный файл и атомарно переименовываем: параллельный запуск не увидит обрезанный exe.
            string tmpPath = targetPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
            File.WriteAllBytes(tmpPath, data);
            if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                try
                {
                    File.SetUnixFileMode(tmpPath,
                        UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute |
                        UnixFileMode.GroupRead | UnixFileMode.GroupExecute |
                        UnixFileMode.OtherRead | UnixFileMode.OtherExecute);
                }
                catch { }
            }
            File.Move(tmpPath, targetPath, overwrite: true);
            if (FileSha256Hex(targetPath) != hash)
            {
                try { File.Delete(targetPath); } catch { }
                return "";
            }
            return targetPath;
        }
        catch { }
        return "";
    }

    // Порядок: выбранный пользователем -> встроенный (проверенный по хешу) -> рядом с приложением /
    // стандартные каталоги установки (ТОЧНЫЕ имена). Раньше дополнительно просматривались текущая
    // директория, Рабочий стол, Загрузки и весь PATH с поиском по маске NanoDecompilerCLI*.exe -
    // любой файл, подложенный в такую папку (или в папку запуска), исполнялся как "движок".
    // Не найден - пустая строка (пользователю предлагается выбрать файл вручную); возвращать голое
    // имя нельзя: Windows ищет такой exe сначала в текущей директории.
    private string FindCliExecutable()
    {
        if (!string.IsNullOrEmpty(_manualCliPath) && File.Exists(_manualCliPath))
            return _manualCliPath;

        string embedded = ExtractEmbeddedCliIfAvailable();
        if (!string.IsNullOrEmpty(embedded) && File.Exists(embedded))
            return embedded;

        var candidateNames = RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
            ? new[] { "NanoDecompilerCLI.exe", "NanoDecompilerClApi.exe", "NanoDecompilerClApi-windows.exe" }
            : new[] { "NanoDecompilerCLI", "NanoDecompilerClApi", "NanoDecompilerCLI-Linux-x64", "NanoDecompilerCLI-macOS-arm64", "NanoDecompilerCLI-macOS-x64" };

        var searchDirs = new List<string>();
        try
        {
            if (!string.IsNullOrEmpty(Environment.ProcessPath))
            {
                var procDir = Path.GetDirectoryName(Environment.ProcessPath);
                if (!string.IsNullOrEmpty(procDir)) searchDirs.Add(procDir);
            }
        }
        catch { }
        try { searchDirs.Add(AppContext.BaseDirectory); } catch { }

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            try
            {
                string pf = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
                if (!string.IsNullOrEmpty(pf)) searchDirs.Add(Path.Combine(pf, "NanoDecompiler"));
            }
            catch { }
            try
            {
                string pf86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
                if (!string.IsNullOrEmpty(pf86)) searchDirs.Add(Path.Combine(pf86, "NanoDecompiler"));
            }
            catch { }
            try
            {
                string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                if (!string.IsNullOrEmpty(localAppData)) searchDirs.Add(Path.Combine(localAppData, "Programs", "NanoDecompiler"));
            }
            catch { }
        }
        else
        {
            searchDirs.Add("/opt/NanoDecompiler");
            searchDirs.Add("/Applications/NanoDecompiler.app/Contents/Resources");
        }

        foreach (var dir in searchDirs.Distinct())
        {
            if (!Directory.Exists(dir)) continue;
            foreach (var sub in new[] { "", Path.Combine("resources", "engine") })
            {
                string d = string.IsNullOrEmpty(sub) ? dir : Path.Combine(dir, sub);
                if (!Directory.Exists(d)) continue;
                foreach (var name in candidateNames)
                {
                    string candidate = Path.Combine(d, name);
                    if (File.Exists(candidate)) return candidate;
                }
            }
        }

        return "";
    }

    private FileNode? _rootNode;

    private void PopulateFileTree(string rootDir)
    {
        FileTreeView.ItemsSource = null;
        _rootNode = CreateDirectoryNode(new DirectoryInfo(rootDir));
        ApplyFilter();
    }

    private void ApplyFilter()
    {
        if (_rootNode == null) return;
        string q = SearchBox.Text?.Trim().ToLowerInvariant() ?? "";
        if (string.IsNullOrEmpty(q))
        {
            FileTreeView.ItemsSource = _rootNode.Children;
        }
        else
        {
            var filtered = FilterNode(_rootNode, q);
            FileTreeView.ItemsSource = filtered?.Children ?? new List<FileNode>();
        }
    }

    private FileNode? FilterNode(FileNode node, string q)
    {
        if (!node.IsDirectory)
        {
            return node.Name.ToLowerInvariant().Contains(q) ? node : null;
        }

        var matchNode = new FileNode
        {
            Name = node.Name,
            FullPath = node.FullPath,
            IsDirectory = true
        };

        foreach (var c in node.Children)
        {
            var matchedChild = FilterNode(c, q);
            if (matchedChild != null)
            {
                matchNode.Children.Add(matchedChild);
            }
        }

        if (matchNode.Children.Count > 0 || node.Name.ToLowerInvariant().Contains(q))
        {
            return matchNode;
        }
        return null;
    }

    private FileNode CreateDirectoryNode(DirectoryInfo dir)
    {
        var node = new FileNode { Name = dir.Name, FullPath = dir.FullName, IsDirectory = true };
        foreach (var sub in dir.GetDirectories())
        {
            node.Children.Add(CreateDirectoryNode(sub));
        }
        foreach (var file in dir.GetFiles())
        {
            node.Children.Add(new FileNode { Name = file.Name, FullPath = file.FullName, IsDirectory = false });
        }
        return node;
    }

    private void OnTreeSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (FileTreeView.SelectedItem is FileNode selected && !selected.IsDirectory)
        {
            try
            {
                // Лимит: раньше любой огромный файл читался целиком и подвешивал интерфейс.
                var info = new FileInfo(selected.FullPath);
                if (info.Length > 4 * 1024 * 1024)
                {
                    CodeEditor.Text = $"Файл слишком большой для просмотра ({info.Length / 1024 / 1024} МБ).";
                    CurrentFileLabel.Text = selected.Name;
                    return;
                }
                string text = File.ReadAllText(selected.FullPath);
                CodeEditor.Text = text;
                CurrentFileLabel.Text = selected.Name;
            }
            catch (Exception ex)
            {
                CodeEditor.Text = $"Ошибка чтения файла: {ex.Message}";
            }
        }
    }

    private async void OnCopyCodeClicked(object? sender, RoutedEventArgs e)
    {
        try
        {
            var topLevel = TopLevel.GetTopLevel(this);
            if (topLevel?.Clipboard != null && !string.IsNullOrEmpty(CodeEditor.Text))
            {
                await topLevel.Clipboard.SetTextAsync(CodeEditor.Text);
                StatusLabel.Text = "Код скопирован в буфер обмена.";
            }
        }
        catch (Exception ex)
        {
            StatusLabel.Text = $"Не удалось скопировать: {ex.Message}";
        }
    }

    private async void OnCheckUpdateClicked(object? sender, RoutedEventArgs e)
    {
        try
        {
            UpdateStatusLabel.Text = "Проверка...";
            await CheckForUpdatesAsync(silent: false);
        }
        catch (Exception ex)
        {
            UpdateStatusLabel.Text = $"Ошибка: {ex.Message}";
        }
    }

    private async Task CheckForUpdatesAsync(bool silent)
    {
        try
        {
            _http.DefaultRequestHeaders.UserAgent.Clear();
            _http.DefaultRequestHeaders.UserAgent.ParseAdd("NanoDecompilerLite-Updater");

            string json = await _http.GetStringAsync("https://api.github.com/repos/NanoDev1488/NanoDecompiler/releases/latest");
            using var doc = JsonDocument.Parse(json);
            string tagName = doc.RootElement.GetProperty("tag_name").GetString() ?? "";
            string remoteVer = tagName.TrimStart('v', 'V');

            if (IsNewer(remoteVer, CurrentVersion))
            {
                UpdateStatusLabel.Text = $"Доступно обновление: v{remoteVer}!";
                if (!silent)
                {
                    StatusLabel.Text = $"Новая версия {tagName} доступна на GitHub. Скачайте в релизах.";
                }
            }
            else
            {
                UpdateStatusLabel.Text = "У вас актуальная версия";
                if (!silent)
                {
                    StatusLabel.Text = $"Установлена последняя версия v{CurrentVersion}.";
                }
            }
        }
        catch (Exception ex)
        {
            if (!silent) StatusLabel.Text = $"Ошибка проверки обновлений: {ex.Message}";
        }
    }

    // Сравнение версий вида "2.0.0" / "2.0.0-alpha": числа по компонентам; при равных числах
    // релиз новее пре-релиза (раньше "0-alpha" не разбиралось как число, и релиз 2.0.0
    // считался НЕ новее установленной 2.0.0-alpha).
    private static bool IsNewer(string remote, string local)
    {
        static (int[] nums, string pre) Parse(string v)
        {
            v = v.Trim().TrimStart('v', 'V');
            int dash = v.IndexOf('-');
            string core = dash >= 0 ? v[..dash] : v;
            string pre = dash >= 0 ? v[(dash + 1)..] : "";
            var nums = core.Split('.').Select(x => int.TryParse(x, out var n) ? n : 0).ToArray();
            return (nums, pre);
        }

        var (rn, rpre) = Parse(remote);
        var (ln, lpre) = Parse(local);
        for (int i = 0; i < Math.Max(rn.Length, ln.Length); i++)
        {
            int r = i < rn.Length ? rn[i] : 0;
            int l = i < ln.Length ? ln[i] : 0;
            if (r > l) return true;
            if (r < l) return false;
        }
        if (rpre.Length == 0 && lpre.Length > 0) return true;   // релиз новее пре-релиза
        if (rpre.Length > 0 && lpre.Length == 0) return false;
        return string.CompareOrdinal(rpre, lpre) > 0;
    }
}

public class FileNode
{
    public string Name { get; set; } = "";
    public string FullPath { get; set; } = "";
    public bool IsDirectory { get; set; }
    public List<FileNode> Children { get; set; } = new();
    public string Icon => IsDirectory ? "📁" : (Name.EndsWith(".java") ? "☕" : "📄");

    public override string ToString() => Name;
}
