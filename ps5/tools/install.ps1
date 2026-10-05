param(
    [Parameter(Mandatory=$true)][string]$Src,
    [Parameter(Mandatory=$true)][string]$Console,
    [int]$FtpPort = 2121,
    [string]$InstallRoot = '/mnt/ext1/etaHEN/games'
)
$ErrorActionPreference = 'Stop'
$python = Get-Command python -ErrorAction SilentlyContinue
if (!$python) { throw 'Python 3 is required. Alternatively, upload with any FTP client.' }
& $python.Source "$PSScriptRoot/install.py" --src $Src --console $Console --ftp-port $FtpPort --install-root $InstallRoot
if ($LASTEXITCODE -ne 0) { throw 'Title upload failed' }
