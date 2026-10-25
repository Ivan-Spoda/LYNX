<?php
// Shared code of the crash-report endpoints (crash.php, reports.php). Not an endpoint itself (.htaccess denies it).
// LYNX (AGPL-3.0-or-later). Reports are opt-in, anonymised by the app before sending (docs: PRIVACY.md).
declare(strict_types=1);

if (basename($_SERVER['SCRIPT_FILENAME'] ?? '') === basename(__FILE__)) {
    http_response_code(404);
    exit;
}

const LYNX_MAX_GZIP = 1048576;  // 1 MB compressed body
const LYNX_MAX_RAW = 4194304;   // 4 MB decoded JSON
const LYNX_PER_IP_PER_DAY = 30; // reports per (hashed) address per day
const LYNX_PER_DAY = 3000;      // all reports per day
const LYNX_MAX_PER_MONTH = 50000;

// Reports live OUTSIDE public_html: a site redeploy cannot touch them and the web server cannot serve them.
// The first candidate PHP can create and write wins; the last one (inside public_html, denied by .htaccess) is only a
// fallback for a host that forbids anything else.
function lynx_storage_dir(): string
{
    static $dir = null;
    if ($dir !== null) {
        return $dir;
    }
    $candidates = [
        dirname(__DIR__, 4) . '/lynx-crash-reports', // the account's home (/home/<user>)
        dirname(__DIR__, 2) . '/lynx-crash-reports', // the domain's folder, next to public_html
        __DIR__ . '/_storage',                       // fallback: public_html/api/_storage (Require all denied)
    ];
    foreach ($candidates as $c) {
        if ((is_dir($c) || @mkdir($c, 0700, true)) && is_writable($c)) {
            return $dir = $c;
        }
    }
    lynx_fail(503, 'storage unavailable');
}

function lynx_json(int $status, array $body): void
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    header('X-Content-Type-Options: nosniff');
    echo json_encode($body, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
}

function lynx_fail(int $status, string $error): void
{
    lynx_json($status, ['ok' => false, 'error' => $error]);
    exit;
}

function lynx_valid_id(string $id): bool
{
    return (bool) preg_match('/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/', $id);
}

// Secret salt for the daily address hashes (created once, never leaves the server)
function lynx_salt(): string
{
    $file = lynx_storage_dir() . '/.salt';
    $salt = @file_get_contents($file);
    if ($salt === false || strlen($salt) < 32) {
        $salt = bin2hex(random_bytes(32));
        file_put_contents($file, $salt, LOCK_EX);
        @chmod($file, 0600);
    }
    return $salt;
}

// Only fatal errors are logged by the host; nothing is printed to the client.
ini_set('display_errors', '0');
set_exception_handler(function (Throwable $e): void {
    error_log('lynx crash api: ' . $e->getMessage());
    if (!headers_sent()) {
        lynx_json(500, ['ok' => false, 'error' => 'server error']);
    }
});
