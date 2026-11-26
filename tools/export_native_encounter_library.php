<?php

// Run with the Bastion site's PHP runtime, passing its deployed source checkout.
// The export is deliberately independent of a player session or discovery data.

use App\Modules\Library\Services\EncounterJournalService;
use Illuminate\Contracts\Console\Kernel;

if ($argc !== 2 || ! is_dir($argv[1])) {
    fwrite(STDERR, "Usage: php export_native_encounter_library.php <Bastion site checkout>\n");
    exit(1);
}

$root = realpath($argv[1]);
require $root.'/vendor/autoload.php';
$app = require $root.'/bootstrap/app.php';
$app->make(Kernel::class)->bootstrap();

$guides = [];
foreach ($app->make(EncounterJournalService::class)->entries() as $entry) {
    if ($entry['status'] !== 'published' || $entry['spoiler']) {
        continue;
    }

    // Ship only player-facing text. Item and loot data require the website's
    // live discovery filter and cannot safely be embedded in an add-on.
    $guides[] = [
        'slug' => $entry['slug'],
        'title' => $entry['title'],
        'zone' => $entry['zone']['name'],
        'zoneShortName' => $entry['zone']['short_name'],
        'zoneVersion' => $entry['zone']['version'],
        'group' => $entry['group'],
        'kind' => $entry['type'],
        'summary' => $entry['summary'],
        'overview' => $entry['overview'],
        'roles' => array_map(
            static fn (array $role): array => [
                'id' => $role['id'], 'label' => $role['label'], 'tips' => $role['tips'],
            ],
            $entry['roles']
        ),
        'phases' => array_map(
            static fn (array $phase): array => [
                'label' => $phase['label'], 'trigger' => $phase['trigger'], 'description' => $phase['description'],
            ],
            $entry['phases']
        ),
        'abilities' => array_map(
            static fn (array $ability): array => [
                'name' => $ability['name'], 'summary' => $ability['summary'], 'description' => $ability['description'],
                'tags' => $ability['tags'], 'roles' => $ability['roles'],
            ],
            $entry['abilities']
        ),
        'sections' => array_values(array_map(
            static fn (array $section): array => [
                'title' => $section['title'], 'paragraphs' => $section['paragraphs'], 'bullets' => $section['bullets'],
            ],
            array_filter($entry['sections'], static fn (array $section): bool => $section['items'] === [])
        )),
    ];
}

if ($guides === []) {
    fwrite(STDERR, "The site's public encounter catalog is empty or disabled.\n");
    exit(1);
}

echo json_encode(['version' => 1, 'guides' => $guides], JSON_THROW_ON_ERROR | JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), "\n";
