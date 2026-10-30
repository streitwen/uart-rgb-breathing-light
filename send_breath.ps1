[CmdletBinding()]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [string]$Color = "0,160,255",
    [double]$Frequency = 0.5,
    [int]$Fps = 50,
    [double]$Gamma = 1.0,
    [double]$Seconds = 0,
    [switch]$Hold,
    [switch]$TestColors,
    [switch]$DryRun,
    [switch]$Quiet,
    [switch]$ListPorts
)

$ErrorActionPreference = "Stop"

function Get-Crc16CcittFalse {
    param([byte[]]$Data)

    [uint32]$crc = 0xFFFF
    foreach ($value in $Data) {
        $crc = $crc -bxor ([uint32]$value -shl 8)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            } else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return [uint16]$crc
}

function New-RgbFrame {
    param([byte]$Red, [byte]$Green, [byte]$Blue)

    [byte[]]$body = @(0xA5, $Red, $Green, $Blue)
    [uint16]$crc = Get-Crc16CcittFalse -Data $body
    return [byte[]]@(
        $body[0],
        $body[1],
        $body[2],
        $body[3],
        [byte]($crc -band 0xFF),
        [byte](($crc -shr 8) -band 0xFF)
    )
}

function Show-Instructions {
    Write-Host ""
    Write-Host "UART RGB breathing-light sender"
    Write-Host ""
    Write-Host "List ports:"
    Write-Host "  .\send_breath.ps1 -ListPorts"
    Write-Host ""
    Write-Host "Offline test:"
    Write-Host "  .\send_breath.ps1 -DryRun -Seconds 6"
    Write-Host ""
    Write-Host "Fixed red:"
    Write-Host '  .\send_breath.ps1 -Port COM5 -Color "255,0,0" -Hold'
    Write-Host ""
    Write-Host "Breathing:"
    Write-Host '  .\send_breath.ps1 -Port COM5 -Color "0,160,255"'
    Write-Host ""
    Write-Host "Cycle fixed test colors:"
    Write-Host "  .\send_breath.ps1 -Port COM5 -TestColors"
    Write-Host ""
}

if ($ListPorts) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    if ($ports.Count -eq 0) {
        Write-Host "No serial ports found."
    } else {
        $ports
    }
    return
}

$parts = $Color -split ","
if ($parts.Count -ne 3) {
    throw "Color must be in R,G,B form, for example 255,0,0."
}

[int]$baseRed = 0
[int]$baseGreen = 0
[int]$baseBlue = 0
if (-not [int]::TryParse($parts[0].Trim(), [ref]$baseRed) -or
    -not [int]::TryParse($parts[1].Trim(), [ref]$baseGreen) -or
    -not [int]::TryParse($parts[2].Trim(), [ref]$baseBlue)) {
    throw "Color values must be integers from 0 to 255."
}
if (($baseRed -lt 0) -or ($baseRed -gt 255) -or
    ($baseGreen -lt 0) -or ($baseGreen -gt 255) -or
    ($baseBlue -lt 0) -or ($baseBlue -gt 255)) {
    throw "Color values must be integers from 0 to 255."
}
if ($Baud -le 0) { throw "Baud must be positive." }
if (($Frequency -le 0) -or ($Frequency -gt 10)) { throw "Frequency must be in (0, 10]." }
if (($Fps -le 0) -or ($Fps -gt 200)) { throw "Fps must be in (0, 200]." }
if (($Gamma -le 0) -or ($Gamma -gt 5)) { throw "Gamma must be in (0, 5]." }
if ($Seconds -lt 0) { throw "Seconds cannot be negative." }
if ((-not $DryRun) -and [string]::IsNullOrWhiteSpace($Port)) {
    Show-Instructions
    throw "Port is required unless -DryRun is used."
}

$serial = $null
if (-not $DryRun) {
    $serial = [System.IO.Ports.SerialPort]::new(
        $Port,
        $Baud,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serial.Handshake = [System.IO.Ports.Handshake]::None
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.WriteTimeout = 500
    $serial.Open()
    Write-Host "Opened $Port at $Baud 8N1."
}

$periodMilliseconds = 1000.0 / $Fps
$nextReport = 0.0
$frameCount = 0
$watch = [System.Diagnostics.Stopwatch]::StartNew()

try {
    while ($true) {
        $elapsed = $watch.Elapsed.TotalSeconds
        if (($Seconds -gt 0) -and ($elapsed -ge $Seconds)) {
            break
        }

        if ($TestColors) {
            $testPalette = @(
                @(255, 0, 0),
                @(0, 255, 0),
                @(0, 0, 255),
                @(255, 255, 255),
                @(0, 0, 0)
            )
            $paletteIndex = [int][Math]::Floor($elapsed / 2.0) % $testPalette.Count
            [int]$red = $testPalette[$paletteIndex][0]
            [int]$green = $testPalette[$paletteIndex][1]
            [int]$blue = $testPalette[$paletteIndex][2]
        } elseif ($Hold) {
            [int]$red = $baseRed
            [int]$green = $baseGreen
            [int]$blue = $baseBlue
        } else {
            [double]$brightness =
                0.5 * (1.0 - [Math]::Cos(2.0 * [Math]::PI * $Frequency * $elapsed))
            if ($Gamma -ne 1.0) {
                $brightness = [Math]::Pow($brightness, $Gamma)
            }
            [int]$red = [Math]::Round($baseRed * $brightness)
            [int]$green = [Math]::Round($baseGreen * $brightness)
            [int]$blue = [Math]::Round($baseBlue * $brightness)
        }

        [byte[]]$frame = New-RgbFrame `
            -Red ([byte]$red) `
            -Green ([byte]$green) `
            -Blue ([byte]$blue)

        if (-not $DryRun) {
            $serial.Write($frame, 0, $frame.Length)
        }
        $frameCount++

        if ((-not $Quiet) -and ($elapsed -ge $nextReport)) {
            $hex = ($frame | ForEach-Object { $_.ToString("X2") }) -join " "
            Write-Host ("{0,8:F3} s  RGB=({1,3},{2,3},{3,3})  {4}" -f
                $elapsed, $red, $green, $blue, $hex)
            $nextReport += 0.25
        }

        $targetMilliseconds = $frameCount * $periodMilliseconds
        $remainingMilliseconds = $targetMilliseconds - $watch.Elapsed.TotalMilliseconds
        if ($remainingMilliseconds -gt 0.5) {
            Start-Sleep -Milliseconds ([int][Math]::Ceiling($remainingMilliseconds))
        }
    }
} finally {
    if (($null -ne $serial) -and $serial.IsOpen) {
        [byte[]]$offFrame = New-RgbFrame -Red 0 -Green 0 -Blue 0
        try {
            $serial.Write($offFrame, 0, $offFrame.Length)
        } catch {
            Write-Warning "Could not send the final off frame: $($_.Exception.Message)"
        }
        $serial.Close()
    }
    $watch.Stop()
}

Write-Host "Stopped after $frameCount frames."
