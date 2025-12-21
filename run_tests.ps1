$BreadLang = ".\breadlang.exe"
$TestDir = "tests"
$Passed = 0
$Failed = 0

Write-Host "Running BreadLang tests..."

# Build interpreter (self-contained)
# Needs GCC installed and in PATH
gcc -std=c11 -Wall -Wextra -O0 -g src\*.c -o breadlang.exe -lm

$TestFiles = Get-ChildItem -Path $TestDir -Recurse -Filter *.bread

foreach ($TestFile in $TestFiles) {
    $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($TestFile.FullName)
    $ExpectedFile = Join-Path $TestFile.Directory.FullName "$BaseName.expected"

    if (-Not (Test-Path $ExpectedFile)) {
        Write-Warning "No expected output file for $($TestFile.FullName)"
        continue
    }

    # PRESERVE NEW LINEES
    $Output = & $BreadLang $TestFile.FullName 2>&1 | Out-String
    $Expected = Get-Content $ExpectedFile -Raw

    # Trim whitespace
    $OutputNormalized = $Output -replace "`r`n", "`n"
    $ExpectedNormalized = $Expected -replace "`r`n", "`n"
    $OutputNormalized = $OutputNormalized.TrimEnd()
    $ExpectedNormalized = $ExpectedNormalized.TrimEnd()

    # Compare for tests
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
