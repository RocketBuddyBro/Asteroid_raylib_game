
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------
# Asteroids-Raylib: automatic project setup, build and run
# ---------------------------------------------------------

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $Root

$ProjectName = Split-Path -Leaf $Root
$RootMain = Join-Path $Root "main.c"
$SrcDir = Join-Path $Root "src"
$SrcMain = Join-Path $SrcDir "main.c"
$BuildDir = Join-Path $Root "build"
$Premake = Join-Path $BuildDir "premake5.exe"
$PremakeLua = Join-Path $BuildDir "premake5.lua"

# Preserve the user's source before changing the structure.
if (Test-Path $RootMain) {
    $OriginalSource = $RootMain
}
elseif (Test-Path $SrcMain) {
    $OriginalSource = $SrcMain
}
else {
    throw "No main.c found in the repository root or src folder."
}

$Temp = Join-Path $env:TEMP (
    "asteroids-setup-" + [guid]::NewGuid().ToString("N")
)

New-Item -ItemType Directory -Path $Temp -Force | Out-Null

$SourceBackup = Join-Path $Temp "user-main.c"
Copy-Item -LiteralPath $OriginalSource -Destination $SourceBackup

try {
    # -----------------------------------------------------
    # 1. Detect the existing Windows C compiler
    # -----------------------------------------------------

    $Toolchain = "C:\raylib\w64devkit\bin"

    if (Test-Path (Join-Path $Toolchain "gcc.exe")) {
        $env:Path = "$Toolchain;$env:Path"
    }

    $Gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue

    $Make = Get-Command make.exe -ErrorAction SilentlyContinue

    if (!$Make) {
        $Make = Get-Command mingw32-make.exe `
            -ErrorAction SilentlyContinue
    }

    if (!$Gcc -or !$Make) {
        throw @"
A GCC compiler and Make are required.

Install raylib with its Windows toolchain, or install W64DevKit.
The script automatically detects:

C:\raylib\w64devkit\bin

Then run this script again.
"@
    }

    Write-Host "GCC and Make detected." -ForegroundColor Green

    # -----------------------------------------------------
    # 2. Download official raylib-quickstart if needed
    # -----------------------------------------------------

    if (!(Test-Path $Premake) -or !(Test-Path $PremakeLua)) {
        Write-Host "Downloading raylib project template..." `
            -ForegroundColor Cyan

        $Zip = Join-Path $Temp "quickstart.zip"
        $ExtractDir = Join-Path $Temp "quickstart"

        $QuickstartUrl = (
            "https://github.com/raylib-extras/" +
            "raylib-quickstart/archive/refs/heads/main.zip"
        )

        Invoke-WebRequest `
            -Uri $QuickstartUrl `
            -OutFile $Zip `
            -UseBasicParsing

        Expand-Archive `
            -LiteralPath $Zip `
            -DestinationPath $ExtractDir `
            -Force

        $Template = Get-ChildItem `
            -LiteralPath $ExtractDir `
            -Directory |
            Select-Object -First 1

        if (!$Template) {
            throw "Could not find the extracted raylib template."
        }

        # Copy the template structure without replacing
        # existing project files.
        foreach ($Name in @(
            ".vscode",
            "build",
            "resources"
        )) {
            $From = Join-Path $Template.FullName $Name
            $To = Join-Path $Root $Name

            if (!(Test-Path $From)) {
                continue
            }

            if (!(Test-Path $To)) {
                Copy-Item `
                    -LiteralPath $From `
                    -Destination $To `
                    -Recurse `
                    -Force
            }
            else {
                # Recover missing template files in an
                # incomplete folder without replacing files.
                Get-ChildItem -LiteralPath $From -Force |
                    ForEach-Object {
                        $Target = Join-Path $To $_.Name

                        if (!(Test-Path $Target)) {
                            Copy-Item `
                                -LiteralPath $_.FullName `
                                -Destination $Target `
                                -Recurse `
                                -Force
                        }
                    }
            }
        }

        # Copy useful optional template files when absent.
        foreach ($Name in @(
            ".clangd",
            "build-MinGW-W64.bat",
            ".gitignore"
        )) {
            $From = Join-Path $Template.FullName $Name
            $To = Join-Path $Root $Name

            if ((Test-Path $From) -and !(Test-Path $To)) {
                Copy-Item `
                    -LiteralPath $From `
                    -Destination $To `
                    -Force
            }
        }
    }

    # -----------------------------------------------------
    # 3. Create the standard project directories
    # -----------------------------------------------------

    foreach ($Folder in @(
        "src",
        "include",
        "resources",
        "build"
    )) {
        New-Item `
            -ItemType Directory `
            -Path (Join-Path $Root $Folder) `
            -Force |
            Out-Null
    }

    if (!(Test-Path $Premake) -or !(Test-Path $PremakeLua)) {
        throw "The raylib build template is incomplete."
    }

    # -----------------------------------------------------
    # 4. Pin the downloaded raylib dependency to 6.2
    # -----------------------------------------------------

    $LuaText = Get-Content -LiteralPath $PremakeLua -Raw

    $LuaText = $LuaText.Replace(
        "https://github.com/raysan5/raylib/archive/refs/heads/master.zip",
        "https://github.com/raysan5/raylib/archive/refs/tags/6.2.zip"
    )

    $LuaText = $LuaText.Replace(
        "raylib-master",
        "raylib-6.2"
    )

    # Write UTF-8 without a BOM to keep the Lua file clean.
    $Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

    [System.IO.File]::WriteAllText(
        $PremakeLua,
        $LuaText,
        $Utf8NoBom
    )

    # -----------------------------------------------------
    # 5. Move the user's game into src/main.c
    # -----------------------------------------------------

    Copy-Item `
        -LiteralPath $SourceBackup `
        -Destination $SrcMain `
        -Force

    # Remove the duplicate root source after preserving it.
    if (Test-Path $RootMain) {
        Remove-Item -LiteralPath $RootMain -Force
    }

    Write-Host "Project structure created." -ForegroundColor Green

    # -----------------------------------------------------
    # 6. Update .gitignore
    # -----------------------------------------------------

    $GitIgnore = Join-Path $Root ".gitignore"

    if (!(Test-Path $GitIgnore)) {
        New-Item -ItemType File -Path $GitIgnore | Out-Null
    }

    $IgnoreRules = @(
        "/bin/",
        "/build/external/",
        "/build/build_files/",
        "/Makefile",
        "/build/*.make",
        "/build/*.mk"
    )

    $ExistingRules = @(
        Get-Content -LiteralPath $GitIgnore `
            -ErrorAction SilentlyContinue
    )

    foreach ($Rule in $IgnoreRules) {
        if ($ExistingRules -notcontains $Rule) {
            Add-Content `
                -LiteralPath $GitIgnore `
                -Value $Rule `
                -Encoding utf8

            $ExistingRules += $Rule
        }
    }

    # -----------------------------------------------------
    # 7. Generate the Makefiles with Premake
    # -----------------------------------------------------

    Write-Host "Generating raylib build files..." `
        -ForegroundColor Cyan

    Push-Location $BuildDir

    try {
        & $Premake gmake

        if ($LASTEXITCODE -ne 0) {
            throw "Premake failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }

    # -----------------------------------------------------
    # 8. Compile the game and raylib
    # -----------------------------------------------------

    Write-Host "Compiling Asteroids..." -ForegroundColor Cyan

    & $Make.Source

    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed with exit code $LASTEXITCODE."
    }

    # -----------------------------------------------------
    # 9. Launch the executable
    # -----------------------------------------------------

    $Exe = Join-Path $Root (
        "bin\Debug\" + $ProjectName + ".exe"
    )

    if (!(Test-Path $Exe)) {
        $BinDir = Join-Path $Root "bin"

        $Candidate = Get-ChildItem `
            -LiteralPath $BinDir `
            -Filter "*.exe" `
            -File `
            -Recurse `
            -ErrorAction SilentlyContinue |
            Select-Object -First 1

        if ($Candidate) {
            $Exe = $Candidate.FullName
        }
    }

    if (!(Test-Path $Exe)) {
        throw "Build completed, but the game executable was not found."
    }

    Write-Host "Launching $ProjectName..." -ForegroundColor Green

    Start-Process `
        -FilePath $Exe `
        -WorkingDirectory $Root

    Write-Host "Done!" -ForegroundColor Green
}
finally {
    # Delete only the temporary setup files.
    if (Test-Path $Temp) {
        Remove-Item `
            -LiteralPath $Temp `
            -Recurse `
            -Force `
            -ErrorAction SilentlyContinue
    }
}
