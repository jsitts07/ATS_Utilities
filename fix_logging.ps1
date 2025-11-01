# Quick fix for logging - replace all fmt-style logging with simple messages
# This allows us to build and test the core functionality

$ErrorActionPreference = "SilentlyContinue"

# Define replacements
$replacements = @{
    'this->info\( "([^"]*)", [^)]+\)' = 'this->info( "$1 [args omitted]" )'
    'this->debug\( "([^"]*)", [^)]+\)' = 'this->debug( "$1 [args omitted]" )'
    'this->error\( "([^"]*)", [^)]+\)' = 'this->error( "$1 [args omitted]" )'
    'this->warning\( "([^"]*)", [^)]+\)' = 'this->warning( "$1 [args omitted]" )'
    'CCore::g_instance->info\( "([^"]*)", [^)]+\)' = 'CCore::g_instance->info( "$1 [args omitted]" )'
    'CCore::g_instance->debug\( "([^"]*)", [^)]+\)' = 'CCore::g_instance->debug( "$1 [args omitted]" )'
    'CCore::g_instance->error\( "([^"]*)", [^)]+\)' = 'CCore::g_instance->error( "$1 [args omitted]" )'
    'CCore::g_instance->warning\( "([^"]*)", [^)]+\)' = 'CCore::g_instance->warning( "$1 [args omitted]" )'
}

# Get all cpp files
$files = Get-ChildItem -Path "src" -Recurse -Filter "*.cpp"

Write-Host "Fixing logging calls in $($files.Count) files..." -ForegroundColor Yellow

foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw
    $modified = $false
    
    foreach ($pattern in $replacements.Keys) {
        $replacement = $replacements[$pattern]
        if ($content -match $pattern) {
            $content = $content -replace $pattern, $replacement
            $modified = $true
        }
    }
    
    if ($modified) {
        Set-Content -Path $file.FullName -Value $content -NoNewline
        Write-Host "Fixed: $($file.Name)" -ForegroundColor Green
    }
}

Write-Host "Logging fix complete. Try building again." -ForegroundColor Green