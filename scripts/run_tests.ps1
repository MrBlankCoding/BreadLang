$RootDir = Split-Path -Parent $PSScriptRoot
$BreadLang = Join-Path $RootDir "breadlang.exe"
$TestDir = Join-Path $RootDir "tests\integration"
$Passed = 0
$Failed = 0

Write-Host "Running BreadLang tests..."

$gccArgs = @(
    '-std=c11',
    '-Wall',
    '-Wextra',
    '-O0',
    '-g',
    (Join-Path $RootDir "src\ast.c"),
    (Join-Path $RootDir "src\bytecode.c"),
    (Join-Path $RootDir "src\compiler.c"),
    (Join-Path $RootDir "src\expr.c"),
    (Join-Path $RootDir "src\expr_ops.c"),
    (Join-Path $RootDir "src\function.c"),
    (Join-Path $RootDir "src\interpreter.c"),
    (Join-Path $RootDir "src\print.c"),
    (Join-Path $RootDir "src\runtime.c"),
    (Join-Path $RootDir "src\semantic.c"),
    (Join-Path $RootDir "src\value.c"),
    (Join-Path $RootDir "src\var.c"),
    (Join-Path $RootDir "src\vm.c"),
    '-o', $BreadLang,
    '-lm'
)

& gcc @gccArgs

$TestFiles = Get-ChildItem -Path $TestDir -Recurse -Filter *.bread

foreach ($TestFile in $TestFiles) {
    $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($TestFile.FullName)
    $ExpectedFile = Join-Path $TestFile.Directory.FullName "$BaseName.expected"

    if (-Not (Test-Path $ExpectedFile)) {
        Write-Warning "No expected output file for $($TestFile.FullName)"
        continue
    }

    $Output = & $BreadLang $TestFile.FullName 2>&1 | Out-String
    $Expected = Get-Content $ExpectedFile -Raw

    $OutputNormalized = $Output -replace "`r`n", "`n"
    $ExpectedNormalized = $Expected -replace "`r`n", "`n"
    $OutputNormalized = $OutputNormalized.TrimEnd()
    $ExpectedNormalized = $ExpectedNormalized.TrimEnd()

    if ($OutputNormalized -eq $ExpectedNormalized) {
        Write-Host "PASS: $BaseName"
        $Passed++
    } else {
        Write-Host "FAIL: $BaseName"
        Write-Host "Expected:"
        Write-Host $Expected
        Write-Host "Got:"
        Write-Host $Output
        $Failed++
    }
}

Write-Host ""
Write-Host "Results: $Passed passed, $Failed failed"

if ($Failed -ne 0) {
    exit 1
}
