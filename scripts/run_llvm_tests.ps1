# PowerShell test runner for LLVM backend tests
# This script runs comprehensive tests for LLVM IR emission, object generation, executable linking, and JIT execution

param(
    [switch]$Verbose,
    [switch]$StopOnError,
    [ValidateSet("all", "emit_ll", "emit_exe", "jit_exec", "code_coverage")]
    [string]$TestType = "all"
)

$RootDir = Split-Path -Parent $PSScriptRoot
$BreadLang = Join-Path $RootDir "breadlang.exe"
$TestDir = Join-Path $RootDir "tests\llvm_backend"
$TempDir = Join-Path $RootDir "llvm_test_temp"

# Results tracking
$Results = @{
    total = 0
    passed = 0
    failed = 0
    skipped = 0
    emit_ll = @{ total = 0; passed = 0; failed = 0 }
    emit_exe = @{ total = 0; passed = 0; failed = 0 }
    jit_exec = @{ total = 0; passed = 0; failed = 0 }
    code_coverage = @{ total = 0; passed = 0; failed = 0 }
}

$FailedTests = @()

# Create temp directory
if (-Not (Test-Path $TempDir)) {
    New-Item -ItemType Directory -Path $TempDir | Out-Null
}

# Check if breadlang exists
if (-Not (Test-Path $BreadLang)) {
    Write-Host "Error: breadlang.exe not found at $BreadLang" -ForegroundColor Red
    Write-Host "Building BreadLang..." -ForegroundColor Yellow
    & (Join-Path $RootDir "build.ps1")
    if (-Not (Test-Path $BreadLang)) {
        Write-Host "Error: Build failed" -ForegroundColor Red
        exit 1
    }
}

function Test-EmitLL {
    param(
        [string]$TestFile,
        [string]$BaseName
    )
    
    $OutputFile = Join-Path $TempDir "$BaseName.ll"
    $Output = & $BreadLang $TestFile --emit-llvm -o $OutputFile 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        return @{ success = $false; message = "Compilation failed: $Output" }
    }
    
    if (-Not (Test-Path $OutputFile)) {
        return @{ success = $false; message = "Output file not generated" }
    }
    
    $Content = Get-Content $OutputFile -Raw
    if (-Not $Content -or $Content.Length -lt 100) {
        return @{ success = $false; message = "Generated LLVM IR is too short or empty" }
    }
    
    # Validate basic LLVM structure
    if (-Not ($Content -match "define.*@main")) {
        return @{ success = $false; message = "Missing main function definition" }
    }
    
    if (-Not ($Content -match "module")) {
        return @{ success = $false; message = "Invalid LLVM module structure" }
    }
    
    return @{ success = $true; message = "LLVM IR generated and verified" }
}

function Test-EmitExe {
    param(
        [string]$TestFile,
        [string]$BaseName,
        [string]$ExpectedFile
    )
    
    if (-Not (Test-Path $ExpectedFile)) {
        return @{ success = $false; message = "No expected output file" }
    }
    
    $ExeFile = Join-Path $TempDir $BaseName
    $Output = & $BreadLang $TestFile --emit-exe -o $ExeFile 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        return @{ success = $false; message = "Executable compilation failed: $Output" }
    }
    
    if (-Not (Test-Path "$ExeFile.exe")) {
        return @{ success = $false; message = "Executable not generated" }
    }
    
    # Run the executable
    $ExecOutput = & "$ExeFile.exe" 2>&1 | Out-String
    $Expected = Get-Content $ExpectedFile -Raw
    
    $OutputNormalized = $ExecOutput -replace "`r`n", "`n"
    $ExpectedNormalized = $Expected -replace "`r`n", "`n"
    $OutputNormalized = $OutputNormalized.TrimEnd()
    $ExpectedNormalized = $ExpectedNormalized.TrimEnd()
    
    if ($OutputNormalized -eq $ExpectedNormalized) {
        return @{ success = $true; message = "Executable runs correctly" }
    }
    else {
        $message = @"
Output mismatch
Expected: $ExpectedNormalized
Got: $OutputNormalized
"@
        return @{ success = $false; message = $message }
    }
}

function Test-JitExec {
    param(
        [string]$TestFile,
        [string]$ExpectedFile
    )
    
    if (-Not (Test-Path $ExpectedFile)) {
        return @{ success = $false; message = "No expected output file" }
    }
    
    $Output = & $BreadLang $TestFile --jit 2>&1 | Out-String
    $Expected = Get-Content $ExpectedFile -Raw
    
    $OutputNormalized = $Output -replace "`r`n", "`n"
    $ExpectedNormalized = $Expected -replace "`r`n", "`n"
    $OutputNormalized = $OutputNormalized.TrimEnd()
    $ExpectedNormalized = $ExpectedNormalized.TrimEnd()
    
    if ($OutputNormalized -eq $ExpectedNormalized) {
        return @{ success = $true; message = "JIT execution produces correct output" }
    }
    else {
        $message = @"
Output mismatch
Expected: $ExpectedNormalized
Got: $OutputNormalized
"@
        return @{ success = $false; message = $message }
    }
}

function Run-Test {
    param(
        [string]$Category,
        [string]$TestFile,
        [string]$BaseName,
        [string]$ExpectedFile
    )
    
    $Results.total++
    $Results[$Category].total++
    
    $TestName = "$Category/$BaseName"
    
    try {
        $Result = $null
        
        switch ($Category) {
            "emit_ll" {
                $Result = Test-EmitLL -TestFile $TestFile -BaseName $BaseName
            }
            "emit_exe" {
                $Result = Test-EmitExe -TestFile $TestFile -BaseName $BaseName -ExpectedFile $ExpectedFile
            }
            "jit_exec" {
                $Result = Test-JitExec -TestFile $TestFile -ExpectedFile $ExpectedFile
            }
            "code_coverage" {
                $Result = Test-JitExec -TestFile $TestFile -ExpectedFile $ExpectedFile
            }
        }
        
        if ($Result.success) {
            Write-Host "✓ PASS: $TestName" -ForegroundColor Green
            $Results.passed++
            $Results[$Category].passed++
        }
        else {
            Write-Host "✗ FAIL: $TestName" -ForegroundColor Red
            if ($Verbose) {
                Write-Host "  $($Result.message)" -ForegroundColor Red
            }
            $Results.failed++
            $Results[$Category].failed++
            $FailedTests += @{
                name = $TestName
                message = $Result.message
            }
            
            if ($StopOnError) {
                throw "Test failed"
            }
        }
    }
    catch {
        Write-Host "✗ ERROR: $TestName - $_" -ForegroundColor Red
        $Results.failed++
        $Results[$Category].failed++
        $FailedTests += @{
            name = $TestName
            message = $_.Exception.Message
        }
        
        if ($StopOnError) {
            throw
        }
    }
}

# Main test execution
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "BreadLang LLVM Backend Test Suite" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$TestCategories = @("emit_ll", "emit_exe", "jit_exec", "code_coverage")
if ($TestType -ne "all") {
    $TestCategories = @($TestType)
}

foreach ($Category in $TestCategories) {
    $CategoryDir = Join-Path $TestDir $Category
    
    if (-Not (Test-Path $CategoryDir)) {
        Write-Host "Skipping $Category - directory not found" -ForegroundColor Yellow
        continue
    }
    
    Write-Host ""
    Write-Host "Running $Category tests..." -ForegroundColor Cyan
    Write-Host "---" -ForegroundColor Cyan
    
    $TestFiles = Get-ChildItem -Path $CategoryDir -Filter *.bread
    
    foreach ($TestFile in $TestFiles) {
        $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($TestFile.Name)
        $ExpectedFile = Join-Path $CategoryDir "$BaseName.expected"
        
        Run-Test -Category $Category -TestFile $TestFile.FullName -BaseName $BaseName -ExpectedFile $ExpectedFile
    }
}

# Cleanup
Write-Host ""
Write-Host "Cleaning up temporary files..." -ForegroundColor Yellow
Remove-Item $TempDir -Recurse -Force -ErrorAction SilentlyContinue

# Summary
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Test Results Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

foreach ($Category in $TestCategories) {
    $Stats = $Results[$Category]
    if ($Stats.total -gt 0) {
        Write-Host ""
        Write-Host "$($Category.ToUpper()):" -ForegroundColor Cyan
        Write-Host "  Total:  $($Stats.total)"
        Write-Host "  Passed: $($Stats.passed)" -ForegroundColor Green
        Write-Host "  Failed: $($Stats.failed)" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "OVERALL RESULTS:" -ForegroundColor Cyan
Write-Host "  Total:  $($Results.total)"
Write-Host "  Passed: $($Results.passed)" -ForegroundColor Green
Write-Host "  Failed: $($Results.failed)" -ForegroundColor Red

if ($Results.failed -gt 0) {
    Write-Host ""
    Write-Host "Failed Tests Details:" -ForegroundColor Red
    foreach ($Failed in $FailedTests) {
        Write-Host "  ✗ $($Failed.name)"
        if ($Verbose) {
            Write-Host "    $($Failed.message)"
        }
    }
    Write-Host ""
    Write-Host "Run with -Verbose for more details" -ForegroundColor Yellow
}

Write-Host ""

# Exit with appropriate code
if ($Results.failed -gt 0) {
    exit 1
}
else {
    exit 0
}