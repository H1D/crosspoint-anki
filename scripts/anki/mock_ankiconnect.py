#!/usr/bin/env python3
"""Minimal AnkiConnect look-alike for end-to-end simulator runs.

Serves the actions crosspoint-anki uses, with the response shapes of the real
add-on (including CSS-prefixed card HTML and the <hr id=answer> answer side).
Logs every request to stderr and to /tmp/mock_ankiconnect.jsonl.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

API_KEY = sys.argv[2] if len(sys.argv) > 2 else None
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

CSS = "<style>.card { font-family: arial; font-size: 20px; text-align: center; color: black; background-color: white; }</style>"

DECKS = {"Default": 1, "Dutch": 2, "Dutch::Common": 3, "Cloze demo": 4, "English::Spelling": 5}
STATS = {
    "Default": (0, 0, 0),
    "Dutch": (2, 1, 3),
    "Dutch::Common": (2, 1, 3),
    "Cloze demo": (1, 0, 0),
    "English::Spelling": (0, 2, 0),
}
CARDS = {
    # Type-in-the-answer cards: the field the marker names is the expected answer.
    1998: {"deck": "English::Spelling", "queue": 1, "type": 1,
           "fields": {"Front": "обещать; обещание", "Word": "<b>promise</b>"},
           "q": "обещать; обещание<br><br>[[type:Word]]",
           "a": "[[type:Word]]<br><br>Dutch: <b>beloven</b>"},
    1999: {"deck": "English::Spelling", "queue": 1, "type": 1,
           "fields": {"Front": "coffee", "Word": "café"},
           "q": "coffee (accents optional)<br><br>[[type:nc:Word]]",
           "a": "[[type:nc:Word]]"},
    2001: {"deck": "Dutch::Common", "queue": 1, "type": 1,
           "q": "<div class=front>het <b>huis</b><br><i>Het huis is groot.</i></div>",
           "a": "the house &amp; home[sound:huis.mp3]"},
    2002: {"deck": "Dutch::Common", "queue": 2, "type": 2,
           "q": "de <strong>fiets</strong>&nbsp;<img src=\"bike.png\">",
           "a": "<div>bicycle</div><div></div><div>Note: <em>fietsen</em> = to cycle</div>"},
    2003: {"deck": "Dutch::Common", "queue": 2, "type": 2,
           "q": "<span style=\"color:red\">lopen</span> [anki:play:q:0]",
           "a": "to walk"},
    2004: {"deck": "Dutch::Common", "queue": 2, "type": 2, "q": "eten", "a": "to eat"},
    2005: {"deck": "Cloze demo", "queue": 0, "type": 0,
           "q": "Amsterdam is the capital of <span class=\"cloze\" data-cloze=\"the Netherlands\" data-ordinal=\"1\">[...]</span>",
           "a": "Amsterdam is the capital of <span class=\"cloze\" data-ordinal=\"1\">the Netherlands</span>"},
    2006: {"deck": "Dutch::Common", "queue": 0, "type": 0, "q": "drinken", "a": "to drink"},
    2007: {"deck": "Dutch::Common", "queue": 0, "type": 0, "q": "slapen", "a": "to sleep"},
}
MODELS = {"Basic": ["Front", "Back"], "Basic (and reversed card)": ["Front", "Back"]}
notes_added = []


def find_cards(query):
    ids = []
    for cid, c in CARDS.items():
        if '"deck:' in query and not any(('"deck:%s"' % d) in query for d in [c["deck"]] + [c["deck"].split("::")[0]]):
            continue
        if "is:learn" in query and "-is:learn" not in query:
            ok = c["queue"] in (1, 3)
        elif "is:due" in query:
            ok = c["queue"] == 2
        elif "is:new" in query:
            ok = c["queue"] == 0
        else:
            ok = True
        if ok:
            ids.append(cid)
    return sorted(ids)


def handle(req):
    action = req.get("action")
    params = req.get("params", {})
    if API_KEY and req.get("key") != API_KEY and action != "requestPermission":
        raise Exception("valid api key must be provided")
    if action == "version":
        return 6
    if action == "loadProfile":
        return params.get("name") in ("User 1", "Alice")
    if action == "deckNames":
        return list(DECKS)
    if action == "getDeckStats":
        out = {}
        for name in params["decks"]:
            n, l, r = STATS.get(name, (0, 0, 0))
            out[str(DECKS[name])] = {"deck_id": DECKS[name], "name": name, "new_count": n,
                                     "learn_count": l, "review_count": r, "total_in_deck": 10}
        return out
    if action == "findCards":
        return find_cards(params["query"])
    if action == "cardsInfo":
        out = []
        for cid in params["cards"]:
            c = CARDS.get(cid)
            if not c:
                continue
            out.append({
                "cardId": cid, "note": cid * 10, "deckName": c["deck"], "modelName": "Basic",
                "fields": {name: {"value": value, "order": i} for i, (name, value) in
                           enumerate(c.get("fields", {"Front": c["q"], "Back": c["a"]}).items())},
                "css": ".card {}", "question": CSS + c["q"],
                "answer": CSS + c["q"] + "<hr id=answer>" + c["a"],
                "queue": c["queue"], "type": c["type"], "interval": 3, "due": 1, "reps": 2, "lapses": 0,
                "left": 0, "mod": 1700000000, "ord": 0, "factor": 2500,
            })
        return out
    if action == "answerCards":
        return [a["cardId"] in CARDS for a in params["answers"]]
    if action == "modelFieldNames":
        if params["modelName"] not in MODELS:
            raise Exception("model was not found: " + params["modelName"])
        return MODELS[params["modelName"]]
    if action == "addNote":
        note = params["note"]
        if note["deckName"] not in DECKS:
            raise Exception("deck was not found: " + note["deckName"])
        if any(n["fields"] == note["fields"] for n in notes_added):
            raise Exception("cannot create note because it is a duplicate")
        notes_added.append(note)
        return 1700000000000 + len(notes_added)
    raise Exception("unsupported action")


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(n)
        try:
            req = json.loads(raw)
            reply = {"result": handle(req), "error": None}
        except Exception as e:  # noqa: BLE001
            req = {"raw": raw.decode("utf-8", "replace")}
            reply = {"result": None, "error": str(e)}
        line = json.dumps({"request": req, "reply": reply}, ensure_ascii=False)
        with open("/tmp/mock_ankiconnect.jsonl", "a") as f:
            f.write(line + "\n")
        sys.stderr.write(line[:300] + "\n")
        body = json.dumps(reply).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):  # quiet
        pass


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
