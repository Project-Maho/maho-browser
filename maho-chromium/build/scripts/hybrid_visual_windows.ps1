[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('sentinel', 'coordinates', 'permission-denied', 'native-input', 'security', 'model-loop', 'recovery', 'fingerprint', 'challenge', 'misclick', 'telemetry', 'turnstile-online', 'model-provider-online')]
    [string]$Scenario,

    [Parameter(Mandatory=$true)]
    [string]$Cli,

    [Parameter(Mandatory=$true)]
    [string]$App,

    [Parameter(Mandatory=$true)]
    [string]$EvidenceDir,

    [Parameter(Mandatory=$false)]
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$script:Browser = $null
$script:CliProcess = $null
$script:Fixture = $null
$script:BrowserExecuted = $false
$script:Transcript = [System.Collections.ArrayList]::new()
$script:ObservedReceipts = [System.Collections.ArrayList]::new()
$script:RequestId = 0

if (-not (Test-Path -LiteralPath $EvidenceDir)) {
    New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
}
$summaryPath = if ([string]::IsNullOrWhiteSpace($Output)) {
    Join-Path $EvidenceDir "windows-summary.json"
} else { $Output }
$summaryParent = Split-Path -Parent $summaryPath
if ($summaryParent -and -not (Test-Path -LiteralPath $summaryParent)) {
    New-Item -ItemType Directory -Path $summaryParent -Force | Out-Null
}

function Write-JsonFile([string]$Path, $Value) {
    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-ReceiptPath([string]$Name) {
    switch ($Name) {
        'fingerprint' { return Join-Path $EvidenceDir 'fingerprint-audit.json' }
        'turnstile-online' { return Join-Path $EvidenceDir 'turnstile-receipt.json' }
        'model-provider-online' { return Join-Path $EvidenceDir 'provider-online-receipt.json' }
        default { return Join-Path $EvidenceDir "$Name-receipt.json" }
    }
}

function Write-FinalSummary([string]$Status, [string[]]$Blockers, $Result, [int]$ExitCode) {
    $summary = [ordered]@{
        platform = 'windows'
        suite = 'stealth'
        scenario = $Scenario
        status = $Status
        blockers = @($Blockers)
        browser_executed = $script:BrowserExecuted
        receipts = @($script:ObservedReceipts)
        result = $Result
        timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    }
    Write-JsonFile $summaryPath $summary
    Write-JsonFile (Join-Path $EvidenceDir 'transcript.json') @($script:Transcript)
    Write-Output ($summary | ConvertTo-Json -Depth 20)
    exit $ExitCode
}

function Write-Blocked([string]$Reason) {
    Write-Cleanup
    $blocked = [ordered]@{
        scenario = $Scenario
        status = 'BLOCKED'
        reason = $Reason
        timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    }
    Write-JsonFile (Get-ReceiptPath $Scenario) $blocked
    [void]$script:Transcript.Add($blocked)
    Write-FinalSummary 'BLOCKED' @($Reason) $blocked 2
}

function Quote-ProcessArgument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Wait-Task($Task, [int]$TimeoutMilliseconds, [string]$TimeoutReason) {
    if (-not $Task.Wait($TimeoutMilliseconds)) { throw $TimeoutReason }
    if ($Task.IsFaulted) { throw $Task.Exception.GetBaseException() }
    return $Task.GetAwaiter().GetResult()
}

function Find-NestedValue($Value, [string]$Name) {
    if ($null -eq $Value) { return $null }
    if ($Value -is [System.Collections.IDictionary]) {
        if ($Value.Contains($Name)) { return $Value[$Name] }
        foreach ($child in $Value.Values) {
            $found = Find-NestedValue $child $Name
            if ($null -ne $found) { return $found }
        }
    } elseif ($Value -is [PSCustomObject]) {
        $property = $Value.PSObject.Properties[$Name]
        if ($null -ne $property) { return $property.Value }
        foreach ($childProperty in $Value.PSObject.Properties) {
            $found = Find-NestedValue $childProperty.Value $Name
            if ($null -ne $found) { return $found }
        }
    } elseif (($Value -is [System.Collections.IEnumerable]) -and -not ($Value -is [string])) {
        foreach ($child in $Value) {
            $found = Find-NestedValue $child $Name
            if ($null -ne $found) { return $found }
        }
    }
    return $null
}

function Start-FixtureServer {
    $listener = [System.Net.HttpListener]::new()
    $portProbe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $portProbe.Start()
    $port = ([System.Net.IPEndPoint]$portProbe.LocalEndpoint).Port
    $portProbe.Stop()
    $prefix = "http://127.0.0.1:$port/"
    $listener.Prefixes.Add($prefix)
    $listener.Start()
    $script:Fixture = $listener
    return $prefix
}

function Send-FixtureResponse($Context, [byte[]]$Body, [string]$ContentType = 'text/html') {
    $Context.Response.StatusCode = 200
    $Context.Response.ContentType = $ContentType
    $Context.Response.ContentLength64 = $Body.Length
    $Context.Response.OutputStream.Write($Body, 0, $Body.Length)
    $Context.Response.OutputStream.Close()
}

function Receive-FixtureRequest($Pending, [string]$ExpectedMethod, [string]$ExpectedPath, [int]$TimeoutMilliseconds) {
    $context = Wait-Task $Pending $TimeoutMilliseconds "fixture_timeout:$ExpectedMethod$ExpectedPath"
    if ($context.Request.HttpMethod -ne $ExpectedMethod -or $context.Request.Url.AbsolutePath -ne $ExpectedPath) {
        $actual = "$($context.Request.HttpMethod)$($context.Request.Url.AbsolutePath)"
        $context.Response.StatusCode = 404
        $context.Response.Close()
        throw "unexpected_fixture_request:$actual"
    }
    return $context
}

function Receive-JsonReceipt($Pending, [string]$ExpectedPath, [int]$TimeoutMilliseconds) {
    $context = Receive-FixtureRequest $Pending 'POST' $ExpectedPath $TimeoutMilliseconds
    try {
        $reader = [System.IO.StreamReader]::new($context.Request.InputStream, $context.Request.ContentEncoding)
        try { $body = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $observed = $body | ConvertFrom-Json
        Send-FixtureResponse $context ([Text.Encoding]::UTF8.GetBytes('{"ok":true}')) 'application/json'
        [void]$script:ObservedReceipts.Add($observed)
        return $observed
    } catch {
        if ($context.Response.OutputStream.CanWrite) { $context.Response.StatusCode = 400; $context.Response.Close() }
        throw
    }
}

function Start-BrowserPipeSession {
    $fixtureUrl = Start-FixtureServer
    $pagePending = $script:Fixture.GetContextAsync()

    $profile = Join-Path ([System.IO.Path]::GetTempPath()) ("maho-windows-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $profile -Force | Out-Null
    $browserLog = Join-Path $EvidenceDir 'browser.stderr.log'
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $App
    $start.Arguments = @(
        Quote-ProcessArgument "--user-data-dir=$profile"
        '--no-first-run'
        '--no-default-browser-check'
        '--maho-disable-login-gate'
        '--enable-logging=stderr'
        Quote-ProcessArgument $fixtureUrl
    ) -join ' '
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.CreateNoWindow = $false
    $script:Browser = [System.Diagnostics.Process]::new()
    $script:Browser.StartInfo = $start
    if (-not $script:Browser.Start()) { throw 'browser_start_failed' }
    $script:BrowserExecuted = $true

    $logWriter = [System.IO.StreamWriter]::new($browserLog, $false, [Text.Encoding]::UTF8)

    $page = Receive-FixtureRequest $pagePending 'GET' '/' 30000
    $readyPending = $script:Fixture.GetContextAsync()
    $fixturePath = Join-Path $PSScriptRoot 'security_fixtures\baseline.html'
    $fixtureBytes = [System.IO.File]::ReadAllBytes($fixturePath)
    Send-FixtureResponse $page $fixtureBytes
    $readyReceipt = Receive-JsonReceipt $readyPending '/receipt' 30000
    if ($readyReceipt.kind -ne 'ready' -or $readyReceipt.value -ne 'Maho ordinary form baseline') {
        throw 'fixture_ready_receipt_mismatch'
    }
    $pipeName = $null
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds(30)
    try {
        while ([DateTimeOffset]::UtcNow -lt $deadline) {
            $remaining = [Math]::Max(1, [int]($deadline - [DateTimeOffset]::UtcNow).TotalMilliseconds)
            $line = Wait-Task $script:Browser.StandardError.ReadLineAsync() $remaining 'browser_mcp_pipe_readiness_timeout'
            if ($null -eq $line) { throw 'browser_exited_before_mcp_pipe_readiness' }
            $logWriter.WriteLine($line)
            $logWriter.Flush()
            if ($line -match 'MCP: Listening on pipe (.+)$') {
                $pipeName = $Matches[1].Trim()
                break
            }
        }
    } finally {
        $logWriter.Dispose()
    }
    if ([string]::IsNullOrWhiteSpace($pipeName)) { throw 'browser_mcp_pipe_readiness_timeout' }

    $cliStart = [System.Diagnostics.ProcessStartInfo]::new()
    $cliStart.FileName = $Cli
    $cliStart.Arguments = (Quote-ProcessArgument '--socket-path') + ' ' + (Quote-ProcessArgument $pipeName) + ' browser pipe'
    $cliStart.UseShellExecute = $false
    $cliStart.RedirectStandardInput = $true
    $cliStart.RedirectStandardOutput = $true
    $cliStart.RedirectStandardError = $true
    $cliStart.CreateNoWindow = $true
    $script:CliProcess = [System.Diagnostics.Process]::new()
    $script:CliProcess.StartInfo = $cliStart
    if (-not $script:CliProcess.Start()) { throw 'cli_pipe_start_failed' }
    return $readyReceipt
}

function Invoke-BrowserRequest([hashtable]$Request) {
    $script:RequestId += 1
    $Request.id = $script:RequestId
    $requestJson = $Request | ConvertTo-Json -Depth 20 -Compress
    $script:CliProcess.StandardInput.WriteLine($requestJson)
    $script:CliProcess.StandardInput.Flush()
    $line = Wait-Task $script:CliProcess.StandardOutput.ReadLineAsync() 30000 'cli_pipe_response_timeout'
    if ([string]::IsNullOrWhiteSpace($line)) { throw 'cli_pipe_closed' }
    $response = $line | ConvertFrom-Json
    [void]$script:Transcript.Add([ordered]@{ request = $Request; response = $response })
    if ($response.id -ne $Request.id) { throw 'cli_pipe_response_id_mismatch' }
    return $response
}

function Stop-ChildProcess($Process) {
    if ($null -ne $Process -and -not $Process.HasExited) {
        $Process.Kill()
        [void]$Process.WaitForExit(10000)
    }
}

function Write-Cleanup {
    Stop-ChildProcess $script:CliProcess
    Stop-ChildProcess $script:Browser
    if ($null -ne $script:Fixture) { $script:Fixture.Stop(); $script:Fixture.Close() }
    $cliStopped = ($null -eq $script:CliProcess) -or $script:CliProcess.HasExited
    $browserStopped = ($null -eq $script:Browser) -or $script:Browser.HasExited
    Write-JsonFile (Join-Path $EvidenceDir 'cleanup.json') ([ordered]@{
        scenario = $Scenario
        cli_stopped = $cliStopped
        browser_stopped = $browserStopped
        fixture_stopped = ($null -eq $script:Fixture) -or (-not $script:Fixture.IsListening)
    })
}

# Preflight always executes and always emits a structured summary on failure.
$preflightBlockers = [System.Collections.ArrayList]::new()
if (-not (Test-Path -LiteralPath $Cli -PathType Leaf)) { [void]$preflightBlockers.Add("cli_binary_missing:$Cli") }
if (-not (Test-Path -LiteralPath $App -PathType Leaf)) { [void]$preflightBlockers.Add("app_binary_missing:$App") }
$nativeScenarios = @('coordinates', 'permission-denied', 'native-input', 'telemetry')
$fixturePath = Join-Path $PSScriptRoot 'security_fixtures\baseline.html'
if (($nativeScenarios -contains $Scenario) -and -not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
    [void]$preflightBlockers.Add("fixture_asset_missing:$fixturePath")
}
if (($nativeScenarios -contains $Scenario) -and -not [System.Environment]::UserInteractive) {
    [void]$preflightBlockers.Add('non_interactive_window_station')
}
if ($preflightBlockers.Count -gt 0) {
    Write-Cleanup
    Write-FinalSummary 'BLOCKED' @($preflightBlockers) $null 2
}

try {
    $versionProcess = Start-Process -FilePath $Cli -ArgumentList @('--version') -NoNewWindow -PassThru -Wait
    if ($versionProcess.ExitCode -ne 0) { Write-Blocked "cli_preflight_failed:$($versionProcess.ExitCode)" }
} catch {
    Write-Blocked "cli_preflight_start_failed:$($_.Exception.Message)"
}

$unsupportedReasons = @{
    sentinel = 'windows_handler_not_implemented:sentinel'
    security = 'windows_handler_not_implemented:security'
    'model-loop' = 'windows_handler_not_implemented:model-loop'
    recovery = 'windows_handler_not_implemented:recovery'
    fingerprint = 'windows_handler_not_implemented:fingerprint'
    challenge = 'windows_handler_not_implemented:challenge'
    misclick = 'windows_handler_not_implemented:misclick'
    telemetry = 'windows_telemetry_receipt_not_implemented'
    'turnstile-online' = 'windows_turnstile_siteverify_not_implemented'
    'model-provider-online' = 'windows_model_provider_rpc_not_implemented'
}
if ($unsupportedReasons.ContainsKey($Scenario)) {
    Write-Blocked $unsupportedReasons[$Scenario]
}

$status = 'FAILED'
$reason = $null
$result = $null
try {
    $fixtureReady = Start-BrowserPipeSession
    $observationResponse = Invoke-BrowserRequest @{ op = 'observe'; mode = 'full' }
    if ($observationResponse.ok -ne $true) {
        $reason = 'observe_rpc_failed'
        $result = [ordered]@{ fixture_ready = $fixtureReady; observe_rpc = $observationResponse }
    } else {
        $frameToken = Find-NestedValue $observationResponse.result 'frame_token'
        if ([string]::IsNullOrWhiteSpace([string]$frameToken)) {
            $reason = 'browser_observation_missing_frame_token'
            $result = [ordered]@{ fixture_ready = $fixtureReady; observe_rpc = $observationResponse }
        } else {
            $receiptPending = $script:Fixture.GetContextAsync()
            $nativeResponse = Invoke-BrowserRequest @{
                op = 'native'
                action = @{ kind = 'click'; click_point_css = @(60, 60); target_rect_css = @(0, 0, 200, 120) }
                frame_token = $frameToken
                lease = 'scoped'
            }
            if ($Scenario -eq 'permission-denied') {
                $rpcCode = Find-NestedValue $nativeResponse.error 'rpc_code'
                $result = [ordered]@{ fixture_ready = $fixtureReady; observe_rpc = $observationResponse; native_rpc = $nativeResponse }
                if ($nativeResponse.ok -eq $false -and $rpcCode -eq -32011) {
                    $status = 'PASS'
                } else {
                    $reason = 'runtime_did_not_return_native_permission_rpc_-32011'
                }
            } elseif ($nativeResponse.ok -ne $true) {
                $reason = 'native_rpc_failed'
                $result = [ordered]@{ fixture_ready = $fixtureReady; observe_rpc = $observationResponse; native_rpc = $nativeResponse }
            } else {
                $delivered = Receive-JsonReceipt $receiptPending '/receipt' 15000
                $result = [ordered]@{ fixture_ready = $fixtureReady; observe_rpc = $observationResponse; native_rpc = $nativeResponse; delivered_receipt = $delivered }
                if ($delivered.kind -eq 'click' -and $delivered.value -eq 1 -and $delivered.isTrusted -eq $true) {
                    $status = 'PASS'
                } else {
                    $reason = 'native_action_produced_no_trusted_click_receipt'
                }
            }
        }
    }
} catch {
    $status = 'BLOCKED'
    $reason = "runtime_prerequisite_unavailable:$($_.Exception.Message)"
    $result = [ordered]@{ error = $_.Exception.Message }
} finally {
    Write-Cleanup
}

$record = [ordered]@{
    scenario = $Scenario
    status = $status
    observation = $result
    timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
}
if ($reason) { $record.reason = $reason }
Write-JsonFile (Get-ReceiptPath $Scenario) $record
if ($status -eq 'PASS') {
    Write-FinalSummary 'PASS' @() $record 0
}
if ($status -eq 'BLOCKED') {
    Write-FinalSummary 'BLOCKED' @($reason) $record 2
}
Write-FinalSummary 'FAILED' @() $record 1
