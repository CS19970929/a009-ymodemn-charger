param(
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProxyHost = '127.0.0.1'
$ProxyPort = 7897
$ProxyUrl = "http://$ProxyHost`:$ProxyPort"
$GitExe = (Get-Command git.exe -ErrorAction Stop).Source

function Test-ProxyPort {
    param(
        [string]$HostName,
        [int]$PortNumber,
        [int]$TimeoutMs = 250
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $async = $client.BeginConnect($HostName, $PortNumber, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMs, $false)) {
            return $false
        }

        $client.EndConnect($async)
        return $true
    }
    catch {
        return $false
    }
    finally {
        $client.Close()
    }
}

function Invoke-GitAuto {
    param(
        [string[]]$GitArguments
    )

    $proxyReady = Test-ProxyPort -HostName $ProxyHost -PortNumber $ProxyPort
    $proxyConfig = if ($proxyReady) {
        @('-c', "http.proxy=$ProxyUrl", '-c', "https.proxy=$ProxyUrl")
    }
    else {
        @('-c', 'http.proxy=', '-c', 'https.proxy=')
    }

    & $GitExe @proxyConfig @GitArguments
    exit $LASTEXITCODE
}

function Get-GlobalGitProxy {
    param(
        [string]$Name
    )

    $values = @(& $GitExe config --global --get-all $Name 2>$null)
    if ($values.Count -eq 0) {
        return @()
    }

    return $values
}

function Show-ProxyStatus {
    $proxyReady = Test-ProxyPort -HostName $ProxyHost -PortNumber $ProxyPort
    $httpProxy = Get-GlobalGitProxy -Name 'http.proxy'
    $httpsProxy = Get-GlobalGitProxy -Name 'https.proxy'
    $httpProxyText = if (@($httpProxy).Count -gt 0) { @($httpProxy) -join ', ' } else { '<unset>' }
    $httpsProxyText = if (@($httpsProxy).Count -gt 0) { @($httpsProxy) -join ', ' } else { '<unset>' }

    Write-Host "git exe      : $GitExe"
    Write-Host "proxy address: $ProxyHost`:$ProxyPort"
    Write-Host ("proxy status : {0}" -f ($(if ($proxyReady) { 'reachable' } else { 'unreachable' })))
    Write-Host ("http.proxy   : {0}" -f $httpProxyText)
    Write-Host ("https.proxy  : {0}" -f $httpsProxyText)
}

function Set-GlobalProxy {
    & $GitExe config --global http.proxy $ProxyUrl
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $GitExe config --global https.proxy $ProxyUrl
    exit $LASTEXITCODE
}

function Clear-GlobalProxy {
    & $GitExe config --global --unset-all http.proxy
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 5) {
        exit $LASTEXITCODE
    }

    & $GitExe config --global --unset-all https.proxy
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 5) {
        exit $LASTEXITCODE
    }
}

function Install-ProfileHook {
    $profilePath = $PROFILE
    $profileDir = Split-Path -Parent $profilePath
    if (-not (Test-Path $profileDir)) {
        New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
    }

    if (-not (Test-Path $profilePath)) {
        New-Item -ItemType File -Path $profilePath -Force | Out-Null
    }

    $scriptPath = $MyInvocation.MyCommand.Path.Replace("'", "''")
    $block = @(
        '',
        '# BEGIN Codex Git Network Auto Switch',
        'function git {',
        '    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Args)',
        "    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File '$scriptPath' @Args",
        '}',
        '# END Codex Git Network Auto Switch'
    ) -join "`r`n"

    $content = Get-Content -Path $profilePath -Raw
    $pattern = '(?s)\r?\n?# BEGIN Codex Git Network Auto Switch.*?# END Codex Git Network Auto Switch\r?\n?'
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, $pattern, '')
    $content = $content.TrimEnd()
    if ($content.Length -gt 0) {
        $content += "`r`n"
    }
    $content += $block
    Set-Content -Path $profilePath -Value $content -Encoding UTF8
    Write-Host "profile updated: $profilePath"
    Write-Host "restart PowerShell to enable automatic git fallback."
}

if ($Args.Count -gt 0) {
    $command = $Args[0].ToLowerInvariant()
    switch ($command) {
        '--proxy-status' {
            Show-ProxyStatus
            exit 0
        }
        '--proxy-on' {
            Set-GlobalProxy
        }
        '--proxy-off' {
            Clear-GlobalProxy
        }
        '--install-profile' {
            Install-ProfileHook
        }
        default {
            Invoke-GitAuto -GitArguments $Args
        }
    }
}
else {
    Invoke-GitAuto -GitArguments @('--version')
}
