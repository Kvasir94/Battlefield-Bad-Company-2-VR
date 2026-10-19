param([string]$Root,[string]$Tests='',[string]$Target='')
& (Join-Path $PSScriptRoot 'qb.ps1') -Root $Root -Tests $Tests -Target $Target -Arch x64
