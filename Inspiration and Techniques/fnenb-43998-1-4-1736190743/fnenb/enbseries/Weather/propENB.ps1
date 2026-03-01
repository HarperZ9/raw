#!/bin/bash
# Program: propogateENB.ps1
# Author: Foxnne
# Usage: ./propogateENB.ps1
# Description: copies input ini values to all other inis


$inputINI = Read-Host -Prompt "Enter the name of the .ini to read interior values from:"

if ($inputINI -eq "exit") { exit }
$RegEx = '[^-\w\.]'
if ($inputINI -eq "" -or $inputINI -match $RegEx) {
    Write-Output "ERROR: Invalid file name, exiting..."
}

$lines = [System.Collections.ArrayList]@()

$environment = $false
$interiorKeyRegex = '^.*interior.*$'
foreach ($line in Get-Content .\$inputINI) {

    if ($line -eq '[ENVIRONMENT]') {
        $environment = $true
    }

    if ($line -eq '[SKY]') {
        break
    }
    if ($line -match $interiorKeyRegex) {
         
        if ($environment -eq $true) {
            $keyvalue = $line.Split("{=}")
            $count = $lines.Add($keyvalue)
        } 

        # $rejoined = "$($keyvalue[0])=$($keyvalue[1])"
        # Write-Output $rejoined
    }
}



foreach ($file in Get-ChildItem -Path * -Include *.ini)
{
    $environment = $false
    $content = [System.Collections.ArrayList]@()

    foreach ($oldline in Get-Content $file) {

        if ($oldline -eq '[ENVIRONMENT]') {
            $environment = $true
        }
    
        if ($oldline -eq '[SKY]') {
            $environment = $false
        }

        if ($oldline -match $interiorKeyRegex -and $environment) {

            $keyvalue = $oldline.Split("{=}")

            foreach( $line in $lines) {
                if ($line[0] -eq $keyvalue[0])
                {
                    $newline = "$($line[0])=$($line[1])"
                    $count = $content.Add($newline)
                }
            }
        } else {
            $count = $content.Add($oldline);
        }

        Set-Content -Path $file -Value $content

    }
}





# foreach ($file in $files)
# {
#     if ((Test-Path $file) -eq 1)
#     {
#         ((Get-Content -Path $file -Raw) -replace 'project_name', $newProjectName) | Set-Content -Path $file
#     } 
# }
