$RootDir = Split-Path -Parent $PSScriptRoot
$BreadLang = Join-Path $RootDir "breadlang.exe"
$TestDir = Join-Path $RootDir "tests\integration"
$Passed = 0
$Failed = 0

Write-Host "Running BreadLang tests..."

& bash (Join-Path $RootDir "build.sh")

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
