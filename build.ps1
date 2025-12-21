# PowerShell build script for BreadLang on Windows

param(
    [switch]$Clean,
    [switch]$Help
)

if ($Help) {
    Write-Host @"
BreadLang Windows Build Script

Usage: .\build.ps1 [Options]

Options:
  -Clean               Clean build artifacts before building
  -Help                Show this help message

Examples:
  .\build.ps1                    # Build with clang
  .\build.ps1 -Clean             # Clean build
"@
    exit 0
}

# Set error action preference to stop on errors
$ErrorActionPreference = "Stop"

# Get the root directory
$RootDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BreadLang = Join-Path $RootDir "breadlang.exe"

Write-Host "Building BreadLang..." -ForegroundColor Cyan
Write-Host "Root directory: $RootDir"

# Clean if requested
if ($Clean) {
    Write-Host "Cleaning build artifacts..." -ForegroundColor Yellow
    if (Test-Path $BreadLang) {
        Remove-Item $BreadLang -Force
        Write-Host "Removed: $BreadLang"
    }
}

# Check if clang is available
Write-Host "Checking for clang..." -ForegroundColor Yellow
$CompilerPath = Get-Command "clang" -ErrorAction SilentlyContinue
if (-not $CompilerPath) {
    Write-Host "Error: clang not found. Please install clang." -ForegroundColor Red
    exit 1
}
Write-Host "clang found: $($CompilerPath.Source)" -ForegroundColor Green

# Check for LLVM support
$LLVMCFlags = ""
$LLVMLDFlags = ""
$LLVMLibs = ""
$LLVMDefs = ""

$LLVMConfig = Get-Command "llvm-config" -ErrorAction SilentlyContinue
if ($LLVMConfig) {
    Write-Host "LLVM found, enabling LLVM backend support" -ForegroundColor Green
    try {
        $LLVMCFlags = (& llvm-config --cflags 2>$null) -join " "
        $LLVMLDFlags = (& llvm-config --ldflags 2>$null) -join " "
        $LLVMLibs = (& llvm-config --libs --system-libs 2>$null) -join " "
        $LLVMDefs = "-DBREAD_HAVE_LLVM=1"
    }
    catch {
        Write-Host "Warning: Could not query LLVM configuration" -ForegroundColor Yellow
        $LLVMDefs = "-DBREAD_NO_LLVM=1"
    }
} else {
    Write-Host "Error: llvm-config not found. Please install LLVM and add it to your PATH." -ForegroundColor Red
    exit 1
}

# Define source files
$SourceFiles = @(
    "src/main.c",
    "src/core/function.c",
    "src/core/value.c",
    "src/core/var.c",
    "src/compiler/ast.c",
    "src/compiler/compiler.c",
    "src/compiler/expr.c",
    "src/compiler/expr_ops.c",
    "src/compiler/semantic.c",
    "src/vm/bytecode.c",
    "src/vm/vm.c",
    "src/ir/bread_ir.c",
    "src/backends/llvm_backend.c",
    "src/codegen/codegen.c",
    "src/runtime/print.c",
    "src/runtime/runtime.c"
)

# Convert to full paths
$SourcePaths = $SourceFiles | ForEach-Object { Join-Path $RootDir $_ }

# Build compiler flags
$CompilerFlags = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-O0",
    "-g",
    "-I`"$RootDir/include`"",
    $LLVMCFlags,
    $LLVMDefs
) | Where-Object { $_ }

# Build linker flags
$LinkerFlags = @(
    $LLVMLDFlags,
    $LLVMLibs,
    "-lm"
) | Where-Object { $_ }

# Build the command
$SourceArgs = $SourcePaths | ForEach-Object { "`"$_`"" }
$AllArgs = $CompilerFlags + $SourceArgs + @("-o", "`"$BreadLang`"") + $LinkerFlags

Write-Host "`nCompiling..." -ForegroundColor Cyan
Write-Host "Command: clang $($AllArgs -join ' ')" -ForegroundColor DarkGray

# Execute compilation
try {
    $null = & clang @AllArgs
    
    # Check if executable was created
    if (Test-Path $BreadLang) {
        Write-Host "`n✓ Build successful!" -ForegroundColor Green
        Write-Host "Executable: $BreadLang" -ForegroundColor Green
        Write-Host "Size: $((Get-Item $BreadLang).Length / 1MB) MB"
        exit 0
    } else {
        Write-Host "Error: Build failed - executable not created" -ForegroundColor Red
        exit 1
    }
}
catch {
    Write-Host "Error: Compilation failed" -ForegroundColor Red
    Write-Host $_.Exception.Message
    exit 1
}