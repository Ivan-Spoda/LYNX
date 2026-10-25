<?php
// POST https://lynx-share.com/api/crash.php — receives one crash / freeze report from LYNX (opt-in, see PRIVACY.md).
// Body: the report JSON, gzip-compressed (Content-Encoding: gzip) or plain. Answers {"ok":true,"id":"<report id>"}.
// The sender's address is never stored: a salted hash of it, valid for one day, only counts reports for the limit.
declare(strict_types=1);
require __DIR__ . '/_common.php';

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    header('Allow: POST');
    lynx_fail(405, 'POST only');
}
$length = (int) ($_SERVER['CONTENT_LENGTH'] ?? 0);
if ($length <= 0) {
    lynx_fail(411, 'empty body');
}
if ($length > LYNX_MAX_RAW) {
    lynx_fail(413, 'too large');
}
$body = file_get_contents('php://input', false, null, 0, LYNX_MAX_RAW + 1);
if ($body === false || $body === '') {
    lynx_fail(400, 'empty body');
}

// gzip (magic 1f 8b) or plain JSON; the decoded size is checked while inflating (no zip bombs)
if (strncmp($body, "\x1f\x8b", 2) === 0) {
    if (strlen($body) > LYNX_MAX_GZIP) {
        lynx_fail(413, 'too large');
    }
    $ctx = inflate_init(ZLIB_ENCODING_GZIP);
    $json = '';
    foreach (str_split($body, 65536) as $chunk) {
        $out = @inflate_add($ctx, $chunk, ZLIB_SYNC_FLUSH);
        if ($out === false) {
            lynx_fail(400, 'bad gzip');
        }
        $json .= $out;
        if (strlen($json) > LYNX_MAX_RAW) {
            lynx_fail(413, 'too large');
        }
    }
} else {
    if (strlen($body) > LYNX_MAX_RAW) {
        lynx_fail(413, 'too large');
    }
    $json = $body;
}

$report = json_decode($json, true, 32);
if (!is_array($report)) {
    lynx_fail(400, 'not JSON');
}

// ---- schema 1 -------------------------------------------------------------------------------------------------
$allowed = ['schema', 'id', 'kind', 'test', 'created', 'app', 'system', 'audio', 'settings', 'crash', 'hang', 'steps',
            'plugins', 'user', 'notes'];
foreach (array_keys($report) as $key) {
    if (!in_array($key, $allowed, true)) {
        lynx_fail(400, 'unknown field');
    }
}
if (($report['schema'] ?? null) !== 1) {
    lynx_fail(400, 'unsupported schema');
}
$id = $report['id'] ?? '';
if (!is_string($id) || !lynx_valid_id($id)) {
    lynx_fail(400, 'bad id');
}
if (!in_array($report['kind'] ?? '', ['crash', 'hang', 'unclean', 'manual'], true)) {
    lynx_fail(400, 'bad kind');
}
if (isset($report['test']) && !is_bool($report['test'])) {
    lynx_fail(400, 'bad test flag');
}
$app = $report['app'] ?? null;
if (!is_array($app) || !is_string($app['version'] ?? null) || !preg_match('/^[0-9A-Za-z.+_ -]{1,40}$/', $app['version'])) {
    lynx_fail(400, 'bad version');
}
foreach (['build', 'commit'] as $k) {
    if (isset($app[$k]) && (!is_string($app[$k]) || !preg_match('/^[0-9A-Za-z.+_: -]{0,64}$/', $app[$k]))) {
        lynx_fail(400, 'bad ' . $k);
    }
}
foreach (['system', 'audio', 'settings', 'crash', 'hang', 'user'] as $k) {
    if (isset($report[$k]) && !is_array($report[$k])) {
        lynx_fail(400, 'bad ' . $k);
    }
}
foreach (['steps', 'plugins'] as $k) {
    if (isset($report[$k]) && (!is_array($report[$k]) || !array_is_list($report[$k]) || count($report[$k]) > 500)) {
        lynx_fail(400, 'bad ' . $k);
    }
}

// ---- limits (per hashed address and day, and for everyone) ----------------------------------------------------
$dir = lynx_storage_dir();
$today = gmdate('Y-m-d');
$ip = $_SERVER['REMOTE_ADDR'] ?? '';
foreach (['HTTP_X_REAL_IP', 'HTTP_X_FORWARDED_FOR'] as $h) { // (behind the host's CDN)
    if (!empty($_SERVER[$h])) {
        $ip = trim(explode(',', (string) $_SERVER[$h])[0]);
        break;
    }
}
$who = substr(hash_hmac('sha256', $today . '|' . $ip, lynx_salt()), 0, 32);
unset($ip);
@mkdir($dir . '/rate', 0700, true);
$rateFile = $dir . '/rate/' . $today . '.json';
$fh = fopen($rateFile, 'c+');
if ($fh === false || !flock($fh, LOCK_EX)) {
    lynx_fail(503, 'busy');
}
$counts = json_decode((string) stream_get_contents($fh), true) ?: [];
$total = array_sum($counts);
if (($counts[$who] ?? 0) >= LYNX_PER_IP_PER_DAY || $total >= LYNX_PER_DAY) {
    flock($fh, LOCK_UN);
    fclose($fh);
    header('Retry-After: 86400');
    lynx_fail(429, 'too many reports today');
}

// ---- store ----------------------------------------------------------------------------------------------------
$month = gmdate('Y-m');
$monthDir = $dir . '/' . $month;
if (!is_dir($monthDir) && !@mkdir($monthDir, 0700, true)) {
    lynx_fail(503, 'storage unavailable');
}
$file = $monthDir . '/' . $id . '.json.gz';
if (!file_exists($file)) { // the same report sent again (a lost answer) is stored once
    if (count(glob($monthDir . '/*.json.gz') ?: []) >= LYNX_MAX_PER_MONTH) {
        lynx_fail(507, 'storage full');
    }
    $report['received'] = gmdate('c');
    $data = gzencode(json_encode($report, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE), 9);
    if ($data === false || file_put_contents($file . '.tmp', $data, LOCK_EX) === false || !rename($file . '.tmp', $file)) {
        lynx_fail(503, 'storage unavailable');
    }
    @chmod($file, 0600);
    $summary = [
        'id' => $id,
        'month' => $month,
        'received' => $report['received'],
        'kind' => $report['kind'],
        'test' => (bool) ($report['test'] ?? false),
        'version' => $app['version'],
        'bytes' => strlen($data),
    ];
    file_put_contents($dir . '/index.jsonl', json_encode($summary, JSON_UNESCAPED_SLASHES) . "\n", FILE_APPEND | LOCK_EX);

    $counts[$who] = ($counts[$who] ?? 0) + 1;
    ftruncate($fh, 0);
    rewind($fh);
    fwrite($fh, json_encode($counts));
}
flock($fh, LOCK_UN);
fclose($fh);

// yesterday's counters are no longer needed
foreach (glob($dir . '/rate/*.json') ?: [] as $old) {
    if (basename($old) !== $today . '.json') {
        @unlink($old);
    }
}

lynx_json(200, ['ok' => true, 'id' => $id]);
