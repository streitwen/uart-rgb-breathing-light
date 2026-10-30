[CmdletBinding()]
param(
    [string]$Port = "",
    [string]$Color = "0,160,255",
    [int]$Seconds = 20,
    [double]$Frequency = 0.5,
    [switch]$Hold
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Port)) {
    $usbSerialPorts = @(
        Get-CimInstance Win32_PnPEntity -Filter `
            "Name LIKE '%CH340%' OR Name LIKE '%CP210%' OR Name LIKE '%FTDI%' OR Name LIKE '%USB-SERIAL%'" |
            ForEach-Object {
                $portMatch = [regex]::Match($_.Name, "COM\d+")
                if ($portMatch.Success) {
                    [pscustomobject]@{
                        DeviceID = $portMatch.Value
                        Name = $_.Name
                    }
                }
            }
    )

    if ($usbSerialPorts.Count -eq 0) {
        throw "No USB serial device was found. Check the USB cable and Device Manager."
    }
    if ($usbSerialPorts.Count -gt 1) {
        $portList = ($usbSerialPorts | ForEach-Object {
            "{0}: {1}" -f $_.DeviceID, $_.Name
        }) -join "`n"
        throw "Multiple USB serial devices were found. Use -Port to select one.`n$portList"
    }

    $Port = $usbSerialPorts[0].DeviceID
}

$commonParameters = @{
    Port = $Port
    Color = $Color
    Seconds = $Seconds
    Frequency = $Frequency
}
if ($Hold) {
    $commonParameters.Hold = $true
}

Write-Host "Port=$Port Color=$Color Duration=$Seconds seconds"
& (Join-Path $PSScriptRoot "send_breath.ps1") @commonParameters
