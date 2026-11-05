# One-time setup of the server stack's secrets. Safe to run again: existing values are kept.
#   powershell -ExecutionPolicy Bypass -File server\setup.ps1
#
# Writes (all git-ignored):
#   server\.env    every password and token of the stack (MQTT logins, InfluxDB, Grafana)
#   server\passwd  hashed MQTT logins, read by the broker
$dir = $PSScriptRoot
$envFile = Join-Path $dir ".env"
$chars = [char[]]"ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789"
function New-Secret([int]$n = 24) { -join (1..$n | ForEach-Object { $chars | Get-Random }) }

# Read existing values (server\.env, and the older test-credentials.txt if present).
$v = [ordered]@{}
foreach ($f in @($envFile, (Join-Path $dir "test-credentials.txt"))) {
    if (Test-Path $f) {
        Get-Content $f | Where-Object { $_ -match '^\w+=' } | ForEach-Object { $k, $val = $_ -split '=', 2; if (-not $v.Contains($k)) { $v[$k] = $val } }
    }
}
# Migrate names used by test-credentials.txt.
if ($v.Contains("CONTROLLER_PASSWORD") -and -not $v.Contains("MQTT_CONTROLLER_PASSWORD")) { $v["MQTT_CONTROLLER_PASSWORD"] = $v["CONTROLLER_PASSWORD"] }
if ($v.Contains("TESTER_PASSWORD") -and -not $v.Contains("MQTT_TESTER_PASSWORD")) { $v["MQTT_TESTER_PASSWORD"] = $v["TESTER_PASSWORD"] }

$wanted = [ordered]@{
    MQTT_CONTROLLER_PASSWORD = { New-Secret 20 }   # login of the controller (user climacontrol)
    MQTT_TESTER_PASSWORD     = { New-Secret 20 }   # login for manual tests (user tester)
    MQTT_TELEGRAF_PASSWORD   = { New-Secret 20 }   # read-only login of the data collector (user telegraf)
    INFLUX_ADMIN_PASSWORD    = { New-Secret 24 }   # InfluxDB web UI, user admin
    INFLUX_TOKEN             = { New-Secret 40 }   # API token used by Telegraf and Grafana
    GRAFANA_ADMIN_PASSWORD   = { New-Secret 20 }   # Grafana, user admin
}
$out = @("# Secrets of the ClimaControl server stack (git-ignored). Created by setup.ps1.")
foreach ($k in $wanted.Keys) {
    if (-not $v.Contains($k) -or -not $v[$k]) { $v[$k] = & $wanted[$k] }
    $out += "$k=$($v[$k])"
}
$out | Set-Content -Encoding ascii $envFile

# Hashed MQTT logins for the broker.
$cmd = "rm -f /x/passwd && touch /x/passwd && " +
       "mosquitto_passwd -b /x/passwd climacontrol '$($v.MQTT_CONTROLLER_PASSWORD)' && " +
       "mosquitto_passwd -b /x/passwd tester '$($v.MQTT_TESTER_PASSWORD)' && " +
       "mosquitto_passwd -b /x/passwd telegraf '$($v.MQTT_TELEGRAF_PASSWORD)'"
# (mosquitto_passwd warns that the file is world readable: irrelevant on a Windows drive, hidden here)
docker run --rm -v "${dir}:/x" eclipse-mosquitto:2.0.20 sh -c "$cmd 2>/dev/null"
if ($LASTEXITCODE -ne 0) { Write-Host "Creating server\passwd failed (is Docker running?)"; exit 1 }

$old = Join-Path $dir "test-credentials.txt"
if (Test-Path $old) { Remove-Item $old; Write-Host "Moved the logins from test-credentials.txt into .env" }
Write-Host "Secrets in $envFile"
