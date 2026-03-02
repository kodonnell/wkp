param(
    [switch]$Clean,
    [switch]$All,
    [string]$Only,
    [switch]$SkipTests,
    [switch]$NoIsolation,
    [switch]$RunLocalTests,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CibwArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$activeCondaEnv = $env:CONDA_DEFAULT_ENV
if ($activeCondaEnv -ne "wkp") {
    throw "Activate the 'wkp' environment first, then rerun. Example: micromamba activate wkp"
}
else {
    Write-Host "Using active environment 'wkp'." -ForegroundColor Yellow
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [string[]]$Arguments
    )

    & $Command @Arguments

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Command $($Arguments -join ' ')"
    }
}

function Ensure-NoIsolationBuildDeps {
    $check = @(
        "-c",
        "import build, packaging; print('ok')"
    )

    try {
        Invoke-Checked -Command "python" -Arguments $check
    }
    catch {
        Write-Host "Installing missing local build dependencies into active 'wkp' environment..." -ForegroundColor Yellow
        Invoke-Checked -Command "python" -Arguments @(
            "-m", "pip", "install", "-U",
            "build", "setuptools", "wheel", "packaging"
        )
    }
}

if ($NoIsolation) {
    Write-Host "Building local wheel without isolation (fast iterative mode)..." -ForegroundColor Green
}
else {
    Write-Host "Testing cibuildwheel locally (Windows)..." -ForegroundColor Green
}

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
Push-Location $scriptRoot

try {
    if ($Clean) {
        Write-Host "Cleaning previous builds..." -ForegroundColor Yellow
        foreach ($path in @("build", "_skbuild", "wheelhouse")) {
            if (Test-Path $path) {
                Remove-Item -Recurse -Force $path
            }
        }
    }
    else {
        Write-Host "Reusing existing build artifacts (use -Clean to force a fresh build)." -ForegroundColor Yellow
    }

    if ($NoIsolation) {
        Ensure-NoIsolationBuildDeps
        Write-Host "`nRunning local wheel build (no isolated env)..." -ForegroundColor Green
        Invoke-Checked -Command "python" -Arguments @("-m", "build", ".", "--wheel", "--no-isolation", "--outdir", "wheelhouse")

        if ($RunLocalTests) {
            $wheel = Get-ChildItem "wheelhouse\*.whl" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
            if ($null -eq $wheel) {
                throw "No wheel was produced in wheelhouse."
            }

            Write-Host "Installing built wheel into current environment for tests..." -ForegroundColor Yellow
            Invoke-Checked -Command "python" -Arguments @("-m", "pip", "install", "--force-reinstall", "$($wheel.FullName)")

            if ($SkipTests) {
                Write-Host "Skipping local tests because -SkipTests was set." -ForegroundColor Yellow
            }
            else {
                Write-Host "Running local tests against installed wheel..." -ForegroundColor Green
                Invoke-Checked -Command "pytest" -Arguments @("-ra", "-v", "--import-mode=importlib", "-o", "pythonpath=src", "tests")
            }
        }
    }
    else {
        $envBackup = @{}
        foreach ($name in @("CIBW_BUILD", "CIBW_TEST_SKIP")) {
            $envBackup[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
        }

        if (-not $All) {
            if (-not $Only) {
                $pyTag = (& python -c "import sys; print(f'cp{sys.version_info.major}{sys.version_info.minor}')").Trim()
                $archTag = if ([Environment]::Is64BitOperatingSystem) { "win_amd64" } else { "win32" }
                $Only = "$pyTag-$archTag"
            }
            $env:CIBW_BUILD = $Only
            Write-Host "Building selector: $Only (use -All to build all configured versions)." -ForegroundColor Yellow
        }

        if ($SkipTests) {
            $env:CIBW_TEST_SKIP = "*"
            Write-Host "Skipping cibuildwheel tests for speed (remove -SkipTests to run tests)." -ForegroundColor Yellow
        }

        Write-Host "`nRunning cibuildwheel..." -ForegroundColor Green
        $cibwArguments = @("--platform", "windows", ".")
        if ($CibwArgs) {
            $cibwArguments += $CibwArgs
        }
        Invoke-Checked -Command "cibuildwheel" -Arguments $cibwArguments

        Write-Host "`nBuild successful!" -ForegroundColor Green
        Write-Host "`nWheels built:" -ForegroundColor Cyan
        Get-ChildItem "wheelhouse\*.whl" | ForEach-Object {
            Write-Host "  $($_.Name)" -ForegroundColor Cyan
        }

        foreach ($entry in $envBackup.GetEnumerator()) {
            if ($null -eq $entry.Value) {
                Remove-Item "Env:$($entry.Key)" -ErrorAction SilentlyContinue
            }
            else {
                [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
            }
        }
    }
}
catch {
    Write-Host "`nBuild failed!" -ForegroundColor Red
    Write-Host $_ -ForegroundColor Red
    exit 1
}
finally {
    Pop-Location
}
