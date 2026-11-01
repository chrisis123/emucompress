param(
	[ValidateSet("debug", "release")]
	[string] $Configuration = "release",
	[string] $Target = ""
)

$ErrorActionPreference = "Stop"

$arguments = @("build", "-p", "xdvdfs-ffi")
if ($Configuration -eq "release") {
	$arguments += "--release"
}
if ($Target) {
	$arguments += @("--target", $Target)
}

Push-Location $PSScriptRoot
try {
	& cargo @arguments
	if ($LASTEXITCODE -ne 0) {
		throw "cargo failed with exit code $LASTEXITCODE"
	}
} finally {
	Pop-Location
}

$configFolder = if ($Configuration -eq "release") { "release" } else { "debug" }
$targetFolder = if ($Target) { Join-Path "target" $Target } else { "target" }
Write-Host "Built xdvdfs-ffi in $(Join-Path $targetFolder $configFolder)."
