<?php

/**
 * MojoZork; a simple, just-for-fun implementation of Infocom's Z-Machine.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

$baseurl = 'https://suinevere.duckdns.org';
$dbname = 'multizork.sqlite3';
$db = NULL;
$title = 'multizork';

if (!function_exists('str_ends_with')) {
    function str_ends_with($haystack, $needle) {
        return $needle !== '' && substr($haystack, -strlen($needle)) === (string)$needle;
    }
}

function print_header($subtitle)
{
    global $title;
    $str = <<<EOS
<html>
  <head>
    <meta http-equiv="Content-Type" content="text/html; charset=utf-8"/>
    <title>$title - $subtitle</title>
    <meta name="twitter:card" content="summary" />
    <meta name="twitter:url" content="https://suinevere.duckdns.org/" />
    <meta property="og:url" content="https://suinevere.duckdns.org/" />
    <meta name="twitter:title" content="$title - $subtitle" />
    <meta property="og:title" content="$title - $subtitle" />
    <meta name="twitter:description" content="Making Zork 1 more social!" />
    <meta property="og:description" content="Making Zork 1 more social!" />
    <style>
      /* Text and background color for light mode */
      body {
        color: #333;
        max-width: 900px;
        margin: 50px;
        margin-left: auto;
        margin-right: auto;
        font-size: 16px;
        line-height: 1.3;
        font-weight: 300;
      }

      .userinput {
        color: #0000AA;
      }

      .gameoutput {
        color: #000000;
      }

      .sysmessage {
        color: #909090;
      }

      .gamecrashed {
        color: #CC0000;
      }

      /* Text and background color for dark mode */
      @media (prefers-color-scheme: dark) {
        body {
          color: #ddd;
          background-color: #222;
        }

        a {
          color: #809fff;
        }

        .gameoutput {
          color: #DDDDDD;
        }

        .userinput {
          color: #9999FF;
        }

        .sysmessage {
          color: #777777;
        }

        .gamecrashed {
          color: #FF0000;
        }
      }
    </style>
  </head>
  <body>
EOS;
    print($str);
}

function print_footer()
{
    $str = <<<EOS
  </body>
</html>
EOS;
    print($str);
}

function fail($response, $url = NULL)
{
    header("HTTP/1.0 $response");
    if ($url != NULL) { header("Location: $url"); }
    exit(1);
}

function fail404() { fail('404 Not Found'); }
function fail503() { fail('503 Service Unavailable'); }

function timestamp_to_string($t)
{
    return strftime('%D %T %Z', $t);
}

function display_instance($hashid)
{
    global $db, $title, $baseurl;
    $stmt = $db->prepare('select * from instances where hashid = :hashid limit 1;');
    $stmt->bindValue(':hashid', "$hashid");
    $results = $stmt->execute();
    if ($instancerow = $results->fetchArray()) {
        print_header("game $hashid");
        print("<p><h1>'$hashid'</h1></p>\n");
        print("<p><ul>\n");
        print("<li>story file: '{$instancerow['story_filename']}'</li>\n");
        print("<li>number of players: {$instancerow['num_players']}</li>\n");
        print("<li>started: " . timestamp_to_string($instancerow['starttime']) . "</li>\n");
        print("<li>last saved: " . timestamp_to_string($instancerow['savetime']) . "</li>\n");
        print("<li>z-machine instructions run: {$instancerow['instructions_run']}</li>\n");
        print("<li>game crashed: " . (($instancerow['crashed'] != 0) ? "YES" : "no") . "</li>\n");
        print("<li>transcripts available for players:");

        $sawone = false;
        $stmt = $db->prepare('select * from players where instance = :instid order by id;');
        $stmt->bindValue(':instid', $instancerow['id']);
        $results = $stmt->execute();
        while ($playerrow = $results->fetchArray()) {
            if (!$sawone) {
                $sawone = true;
                print(" [");
            } else {
                print(" |");
            }
            print(" <a href='$baseurl/player/$hashid/{$playerrow['id']}'>" . htmlspecialchars($playerrow['username']) . "</a>");
        }

        if (!$sawone) {
            print(" (no transcripts found!?)");
        } else {
            print(" ]");
        }

        print("</li>\n</ul></p>\n");
        print_footer();
    } else {
        fail404();
    }
}

function display_player($hashid, $playerid, $raw)
{
    global $db, $title, $baseurl;
    $stmt = $db->prepare('select p.username, p.game_over, i.crashed from players as p inner join instances as i on p.instance=i.id where i.hashid = :hashid and p.id = :playerid limit 1;');
    $stmt->bindValue(':hashid', "$hashid");
    $stmt->bindValue(':playerid', "$playerid");
    $results = $stmt->execute();
    if ($row = $results->fetchArray()) {
        $game_over = $row['game_over'];
        $crashed = $row['crashed'];
        $escuname = htmlspecialchars($row['username']);
        print_header("player $escuname - game '$hashid'");
        print("<p><h1>Transcript for player '$escuname'</h1></p>\n");
        print("<p><a href='$baseurl/game/$hashid'>[ game details</a> | ");
        if ($raw) {
            print("<a href='$baseurl/player/$hashid/$playerid'>pretty HTML version</a> ]</p>\n");
        } else {
            print("<a href='$baseurl/rawplayer/$hashid/$playerid'>raw text version</a> ]</p>\n");
        }

        $stmt = $db->prepare('select * from transcripts where player = :playerid order by id;');
        $stmt->bindValue(':playerid', $playerid);
        $results = $stmt->execute();

        if ($raw) {
            print("<pre>\n");
            while ($row = $results->fetchArray()) {
                $str = str_replace("\r\n", "\n", $row['content']);
                print(htmlspecialchars($str));
            }
            if ($crashed != 0) {
                print("\n\n *** GAME INSTANCE CRASHED HERE ***\n\n");
            }
            print("</pre>\n");
        } else {
            while ($row = $results->fetchArray()) {
                $texttype = $row['texttype'];
                if ($texttype == 0) {
                    $divclass = 'gameoutput';
                } else if ($texttype == 1) {
                    $divclass = 'userinput';
                } else {
                    $divclass = 'sysmessage';
                }

                $text = $row['content'];
                $fixprompt = ($texttype == 2) && str_ends_with($text, "\n>");
                if ($fixprompt) {
                    $text = substr($text, 0, strlen($text) - 1);
                }
                $text = str_replace("\r\n", "\n", $text);
                $esctext = str_replace("\n", "<br/>", htmlspecialchars($text));
                print("<span class='$divclass'>$esctext</span>");
                if ($fixprompt) {
                    print("<span class='gameoutput'>&gt;</span>");
                }
            }

            if ($game_over != 0) {
                print("<br/><br/><span class='gamecrashed'>*** GAME OVER ***</span><br/><br/>");
            }

            if ($crashed != 0) {
                print("<br/><br/><span class='gamecrashed'>*** GAME INSTANCE CRASHED HERE ***</span><br/><br/>");
            }
        }
        print_footer();
    } else {
        fail404();
    }
}

// Mainline!

$db = new SQLite3($dbname, SQLITE3_OPEN_READONLY);
if ($db == NULL) {
    fail503();
}

$reqargs = explode('/', preg_replace('/^\/?(.*?)\/?$/', '$1', $_SERVER['PHP_SELF']));
$reqargcount = count($reqargs);
//print_r($reqargs);

$operation = ($reqargcount >= 1) ? $reqargs[0] : '';
$document = ($reqargcount >= 2) ? $reqargs[1] : '';
$extraarg = ($reqargcount >= 3) ? $reqargs[2] : '';

if (($operation == 'game') && ($document != '')) {
    display_instance($document);
} else if (($operation == 'player') && ($document != '') && ($extraarg != '')) {
    display_player($document, $extraarg, false);
} else if (($operation == 'rawplayer') && ($document != '') && ($extraarg != '')) {
    display_player($document, $extraarg, true);
} else {
    fail404();
}

exit(0);
?>
