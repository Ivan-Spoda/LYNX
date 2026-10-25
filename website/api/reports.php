<?php
// Admin access to the stored crash reports (the LYNX maintainer only).
// Authorization: Bearer <key> (or X-Admin-Key: <key>); only the key's SHA-256 is here.
//   GET  reports.php?action=list[&month=YYYY-MM]   -> {"ok":true,"reports":[{id,month,received,kind,test,version,bytes}]}
//   GET  reports.php?action=get&id=<id>            -> the report (application/gzip, the JSON gzip-compressed)
//   POST reports.php?action=delete&id=<id>         -> {"ok":true}
declare(strict_types=1);
require __DIR__ . '/_common.php';

const LYNX_ADMIN_KEY_SHA256 = '4fc10d1735ddf65f7449f29db399757092890adfb508acbac372692151c4a9d3';

$key = '';
$auth = $_SERVER['HTTP_AUTHORIZATION'] ?? ($_SERVER['REDIRECT_HTTP_AUTHORIZATION'] ?? '');
if (preg_match('/^Bearer\s+(\S+)$/', (string) $auth, $m)) {
    $key = $m[1];
} elseif (!empty($_SERVER['HTTP_X_ADMIN_KEY'])) {
    $key = (string) $_SERVER['HTTP_X_ADMIN_KEY'];
}
if ($key === '' || !hash_equals(LYNX_ADMIN_KEY_SHA256, hash('sha256', $key))) {
    usleep(300000);
    lynx_fail(401, 'unauthorized');
}

$dir = lynx_storage_dir();
$action = (string) ($_GET['action'] ?? 'list');

function report_file(string $dir, string $id): ?string
{
    if (!lynx_valid_id($id)) {
        lynx_fail(400, 'bad id');
    }
    $found = glob($dir . '/[0-9][0-9][0-9][0-9]-[0-9][0-9]/' . $id . '.json.gz') ?: [];
    return $found[0] ?? null;
}

switch ($action) {
    case 'list':
        $month = (string) ($_GET['month'] ?? '');
        if ($month !== '' && !preg_match('/^\d{4}-\d{2}$/', $month)) {
            lynx_fail(400, 'bad month');
        }
        $list = [];
        $seen = [];
        $index = @file($dir . '/index.jsonl', FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) ?: [];
        foreach ($index as $line) {
            $r = json_decode($line, true);
            if (!is_array($r) || !lynx_valid_id((string) ($r['id'] ?? '')) || isset($seen[$r['id']])) {
                continue;
            }
            if ($month !== '' && ($r['month'] ?? '') !== $month) {
                continue;
            }
            if (is_file($dir . '/' . $r['month'] . '/' . $r['id'] . '.json.gz')) {
                $seen[$r['id']] = true;
                $list[] = $r;
            }
        }
        lynx_json(200, ['ok' => true, 'reports' => $list]);
        break;

    case 'get':
        $file = report_file($dir, (string) ($_GET['id'] ?? ''));
        if ($file === null) {
            lynx_fail(404, 'not found');
        }
        header('Content-Type: application/gzip');
        header('Cache-Control: no-store');
        header('Content-Length: ' . filesize($file));
        readfile($file);
        break;

    case 'delete':
        if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
            lynx_fail(405, 'POST only');
        }
        $file = report_file($dir, (string) ($_GET['id'] ?? ''));
        if ($file === null) {
            lynx_fail(404, 'not found');
        }
        unlink($file);
        lynx_json(200, ['ok' => true]);
        break;

    case 'where': // the storage folder (to check it is outside public_html)
        lynx_json(200, ['ok' => true, 'storage' => $dir, 'public' => str_starts_with(realpath($dir) ?: $dir, realpath(dirname(__DIR__)) ?: dirname(__DIR__))]);
        break;

    default:
        lynx_fail(400, 'unknown action');
}
