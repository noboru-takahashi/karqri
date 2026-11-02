"""Render cooperative_three_task collector JSONL as a self-contained HTML file."""

import argparse
from collections import Counter
import html
import json
from pathlib import Path
import re
import sys

from capture import Validation


PHASE = "cooperative_three_task"
EVENTS = ("THREAD_BOOTSTRAP", "TASK_MARK", "YIELD", "REQUEST_SET", "REPLACE",
          "CANCEL", "HANDOFF_COMMIT", "TASK_RETURN", "RUN_STOP")
POINTERS = ("active", "current", "pending", "subject")
NUMBERS = ("seq", "us", "value", "source", "flags")


def fields(line):
    result = {}
    for token in line.split()[1:]:
        key, value = token.split("=", 1)
        if key in result:
            raise ValueError("duplicate field " + key)
        result[key] = value
    return result


def number(value, maximum=0xFFFFFFFF, hexadecimal=False):
    pattern = r"0x[0-9a-fA-F]+" if hexadecimal else r"[0-9]+"
    if not re.fullmatch(pattern, value):
        raise ValueError("invalid integer " + repr(value))
    result = int(value, 16 if hexadecimal else 10)
    if result > maximum:
        raise ValueError("integer out of range " + repr(value))
    return result


def pointer(value):
    return f"0x{number(value, hexadecimal=True):08x}"


def analyze(rows, input_issues=(), attempt=None):
    """Do not combine separate run IDs or serial connection attempts."""
    issues = list(input_issues)
    sessions = {}
    for row in rows:
        if row.get("event") == "line" and isinstance(row.get("text"), str):
            key = (str(row.get("run_id", "missing")), str(row.get("attempt", "missing")))
            sessions.setdefault(key, []).append(row["text"])
    candidates = [key for key, lines in sessions.items()
                  if any(line.startswith("PICO_TRACE") for line in lines)
                  and (attempt is None or key[1] == str(attempt))]
    if not candidates:
        issues.append("No trace-containing connection attempt found")
        candidates = [key for key in sessions if attempt is None or key[1] == str(attempt)]
    if len(candidates) > 1:
        issues.append("Multiple trace sessions; only the first is displayed. Select --attempt or split the log")
    key = candidates[0] if candidates else ("missing", "missing")
    lines = sessions.get(key, [])
    validation = Validation()
    for line in lines:
        validation.line(line)
    verdict = validation.result or validation.expired()
    validation_status, validation_reason = verdict.status, verdict.reason
    if input_issues and validation_status == "PASS":
        validation_status, validation_reason = "UNKNOWN", "Damaged JSONL may hide validation output"
    begins, ends, records = [], [], []
    context_names, task_names, name_contexts, name_tasks = {}, {}, {}, {}
    trace_open = False
    closed = False
    for line in lines:
        if not line.startswith("PICO_TRACE"):
            continue
        try:
            f = fields(line)
        except ValueError as exc:
            issues.append("Malformed trace line: " + str(exc))
            continue
        if line.startswith("PICO_TRACE_BEGIN "):
            begins.append(f)
            trace_open, closed = True, False
        elif line.startswith("PICO_TRACE_END "):
            if not trace_open:
                issues.append("END without an open BEGIN")
            ends.append(f)
            trace_open, closed = False, True
        elif line.startswith("PICO_TRACE_TASK "):
            try:
                if not trace_open or closed:
                    issues.append("Task mapping outside BEGIN/END")
                name = f["name"]
                context, task = pointer(f["context"]), pointer(f["task"])
                if name not in ("A", "D", "G") or context == "0x00000000" or task == "0x00000000":
                    raise ValueError("invalid A/D/G mapping")
                if (context in context_names or task in task_names or name in name_contexts):
                    raise ValueError("duplicate/ambiguous Task mapping")
                context_names[context], task_names[task] = name, name
                name_contexts[name], name_tasks[name] = context, task
            except (KeyError, ValueError) as exc:
                issues.append("Task mapping: " + str(exc))
        elif line.startswith("PICO_TRACE "):
            if not trace_open or closed:
                issues.append("Record outside BEGIN/END")
            record = {"index": len(records), "raw": line, "event": f.get("event", "?"), "raw_fields": f}
            for name in NUMBERS:
                try:
                    record[name] = number(f[name], 255 if name in ("source", "flags") else 0xFFFFFFFF,
                                          hexadecimal=name == "flags")
                    if name == "seq" and record[name] == 0:
                        raise ValueError("sequence zero")
                except (KeyError, ValueError) as exc:
                    record[name] = None
                    issues.append(f"Record {record['index'] + 1} {name}: {exc}")
            for name in POINTERS:
                try:
                    record[name] = pointer(f[name])
                except (KeyError, ValueError) as exc:
                    record[name] = "?"
                    issues.append(f"Record {record['index'] + 1} {name}: {exc}")
            if record["event"] not in EVENTS:
                issues.append("Unsupported event: " + record["event"])
            records.append(record)
        else:
            issues.append("Unknown trace line: " + line)

    if len(begins) != 1 or len(ends) != 1:
        issues.append(f"Expected one BEGIN and END; found {len(begins)} / {len(ends)}")
    begin = begins[0] if begins else {}
    end = ends[0] if ends else {}
    if begin.get("phase") != PHASE or end.get("phase") != begin.get("phase"):
        issues.append("BEGIN/END phase mismatch or unsupported phase")
    if begin.get("version") != "1":
        issues.append("Unsupported or missing trace version")
    metadata = {}
    for label, obj, required in (("BEGIN", begin, ("count", "overwritten", "seq_overflow", "time_valid")),
                                 ("END", end, ("count", "overwritten"))):
        for name in required:
            try:
                metadata[label + "_" + name] = number(obj[name])
            except (KeyError, ValueError) as exc:
                metadata[label + "_" + name] = None
                issues.append(label + " " + name + ": " + str(exc))
    if not (metadata["BEGIN_count"] == metadata["END_count"] == len(records)):
        issues.append("BEGIN/END/actual record counts differ")
    if not records or len(records) > 256:
        issues.append("Record count outside 1..256")
    if not (metadata["BEGIN_overwritten"] == metadata["END_overwritten"] == 0):
        issues.append("Records overwritten or BEGIN/END overwritten mismatch")
    if metadata["BEGIN_seq_overflow"] != 0:
        issues.append("Sequence overflow or missing seq_overflow=0")
    if metadata["BEGIN_time_valid"] != 1:
        issues.append("Capture time invalid or missing time_valid=1")
    if set(name_contexts) != {"A", "D", "G"}:
        issues.append("Incomplete A/D/G Task mapping")

    seqs = [r["seq"] for r in records]
    if seqs != list(range(1, len(records) + 1)):
        issues.append("Sequences are missing, duplicated, out of order, or do not start at 1")
    times = [r["us"] for r in records]
    if any(t is None for t in times) or any(a > b for a, b in zip(times, times[1:]) if a is not None and b is not None):
        issues.append("Record time invalid or decreases; chronology is not trustworthy")
    counts = Counter(seqs)
    by_seq = {r["seq"]: r for r in records if r["seq"] is not None and counts[r["seq"]] == 1}
    links, live_request, consumed = [], None, set()
    for record in records:
        for name in POINTERS:
            mapping = task_names if name == "current" else context_names
            addr = record[name]
            record[name + "_name"] = "-" if addr == "0x00000000" else mapping.get(addr, "?")
            if addr not in ("0x00000000", "?") and addr not in mapping:
                issues.append(f"seq {record['seq']}: unmapped {name} pointer {addr}")
        event = record["event"]
        record["mark"] = chr(record["value"]) if event == "TASK_MARK" and record["value"] in range(65, 74) else ""
        if event == "TASK_MARK" and not record["mark"]:
            issues.append(f"seq {record['seq']}: TASK_MARK is not ASCII A..I")
        if event in ("REQUEST_SET", "REPLACE"):
            if event == "REQUEST_SET" and live_request is not None:
                issues.append(f"seq {record['seq']}: SET without consuming/replacing old request")
            if event == "REPLACE" and (live_request is None or record["subject"] != live_request["pending"]):
                issues.append(f"seq {record['seq']}: REPLACE does not match old pending")
            if record["pending_name"] not in ("A", "D", "G"):
                issues.append(f"seq {record['seq']}: request has no mapped destination")
            live_request = record
        elif event == "CANCEL":
            if record["pending"] != "0x00000000":
                issues.append(f"seq {record['seq']}: CANCEL leaves pending")
            live_request = None
        elif event == "HANDOFF_COMMIT":
            request = by_seq.get(record["value"])
            valid = (request is not None and request["event"] in ("REQUEST_SET", "REPLACE")
                     and request is live_request and request["index"] < record["index"]
                     and request["seq"] not in consumed
                     and request["pending"] == record["active"]
                     and request["active"] == record["subject"]
                     and record["pending"] == "0x00000000")
            if not valid:
                issues.append(f"seq {record['seq']}: invalid/stale request sequence reference {record['value']}")
            else:
                links.append({"request": request["index"], "commit": record["index"]})
                consumed.add(request["seq"])
            live_request = None
    if live_request is not None:
        issues.append("Unconsumed request at end of trace")
    issues = list(dict.fromkeys(issues))
    simulation = any(row.get("event") == "run" and row.get("source") == "simulation"
                     and str(row.get("run_id", "missing")) == key[0] for row in rows)
    return dict(phase=begin.get("phase", PHASE), run_id=key[0], attempt=key[1],
                validation=validation_status, validation_reason=validation_reason,
                trace="INCOMPLETE" if issues else "COMPLETE", issues=issues,
                records=records, links=links, tasks=[dict(name=n, context=name_contexts[n], task=name_tasks[n])
                for n in ("A", "D", "G") if n in name_contexts],
                count=len(records), handoffs=sum(r["event"] == "HANDOFF_COMMIT" for r in records),
                overwritten=metadata["BEGIN_overwritten"], simulation=simulation,
                marks="".join(r["mark"] for r in records),
                begin=begin, end=end)


def read_log(path, attempt=None):
    rows, issues = [], []
    with path.open(encoding="utf-8-sig") as source:
        for index, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
                if not isinstance(row, dict):
                    raise ValueError("expected JSON object")
                rows.append(row)
            except (ValueError, json.JSONDecodeError) as exc:
                issues.append(f"JSONL line {index}: {exc}")
    return analyze(rows, issues, attempt)


STYLE = """
body{margin:0;background:#f4f6fa;color:#17233b;font:15px system-ui,sans-serif}
main{max-width:1500px;margin:auto;padding:24px}h1{font-size:25px;margin:0 0 8px}
.cards{display:flex;gap:12px;flex-wrap:wrap;margin:20px 0}.card{background:white;padding:14px 20px;border-radius:10px;border:1px solid #ccd5e3}
.good{border-left:6px solid #16835a}.bad{border-left:6px solid #bc452c}
.muted{color:#52627a}code,pre,td{font-family:ui-monospace,monospace}
.panel{background:white;border:1px solid #ccd5e3;border-radius:10px;padding:16px;margin:16px 0}
#chart{overflow:auto}svg{display:block;background:#fff}svg text{font:12px system-ui}
.marker{cursor:pointer;stroke-width:2}.marker:focus{outline:2px solid #17233b}.selected{stroke:#000;stroke-width:4}
label{display:inline-block;padding:5px 10px}input{vertical-align:middle}
.table-wrap{overflow:auto;max-height:650px}table{border-collapse:collapse;width:100%;font-size:12px}
th,td{padding:7px;border-bottom:1px solid #dce2ec;text-align:left;white-space:nowrap}th{position:sticky;top:0;background:#edf2f8}
tr.selected{background:#fff1cb}pre{white-space:pre-wrap;overflow-wrap:anywhere;margin:0}li{margin:6px 0}
"""

SCRIPT = r"""
'use strict';
const data=JSON.parse(document.getElementById('trace-data').textContent);
const ns='http://www.w3.org/2000/svg';
const events=[...new Set(data.records.map(r=>r.event))];
const visible=new Set(events), filters=document.getElementById('filters');
const colors={THREAD_BOOTSTRAP:'#6b4caf',TASK_MARK:'#08795d',YIELD:'#b36900',REQUEST_SET:'#236cbe',REPLACE:'#236cbe',CANCEL:'#b23b32',HANDOFF_COMMIT:'#b23a89',TASK_RETURN:'#ad4632',RUN_STOP:'#26364e'};
const symbols={THREAD_BOOTSTRAP:'B',YIELD:'Y',REQUEST_SET:'R',REPLACE:'R*',CANCEL:'X',HANDOFF_COMMIT:'H',TASK_RETURN:'T',RUN_STOP:'S'};
function element(tag,text){const e=document.createElement(tag);if(text!==undefined)e.textContent=text;return e;}
function svgElement(tag,attrs,text){const e=document.createElementNS(ns,tag);for(const [k,v] of Object.entries(attrs))e.setAttribute(k,String(v));if(text!==undefined)e.textContent=text;return e;}
let selected=null;
function selectRecord(index){selected=index;const r=data.records[index];document.getElementById('detail').textContent=
  `seq=${r.seq}  us=${r.us}  ${r.event}\nactive(context)=${r.active_name} ${r.active}\ncurrent(Task)=${r.current_name} ${r.current}\npending(context)=${r.pending_name} ${r.pending}\nsubject(context)=${r.subject_name} ${r.subject}\nvalue=${r.value}  source=${r.source}  flags=${r.raw_fields.flags||'?'}\n${r.event==='HANDOFF_COMMIT'?'exception return直前。Task命令実行の保証ではありません。\n':''}${r.raw}`;
  document.querySelectorAll('tr[data-index]').forEach(e=>e.classList.toggle('selected',Number(e.dataset.index)===index));
  const row=document.getElementById('record-'+index);if(row)row.scrollIntoView({block:'nearest'});
  draw();
}
for(const event of events){const label=element('label'),input=element('input');input.type='checkbox';input.checked=true;
  input.addEventListener('change',()=>{if(input.checked)visible.add(event);else visible.delete(event);draw();});
  label.append(input,document.createTextNode(' '+event));label.style.color=colors[event]||'#555';filters.append(label);
}
function draw(){const container=document.getElementById('chart');container.replaceChildren();
  const width=1200*Number(document.getElementById('zoom').value),height=470,left=85,right=35;
  const valid=data.records.filter(r=>Number.isInteger(r.us)&&r.us>=0&&r.us<=4294967295);
  const end=valid.reduce((maximum,r)=>Math.max(maximum,r.us),1);
  const x=r=>left+r.us/end*(width-left-right);
  const lane=r=>({A:110,D:220,G:330}[r.active_name]||420);
  const y=r=>lane(r)+(Math.max(0,events.indexOf(r.event))-4)*8;
  const svg=svgElement('svg',{width,height,viewBox:`0 0 ${width} ${height}`,role:'img','aria-label':'Task trace, hardware timer microseconds'});container.append(svg);
  for(const [name,yy] of [['A',110],['D',220],['G',330],['?',420]]){
    svg.append(svgElement('line',{x1:left,y1:yy,x2:width-right,y2:yy,stroke:'#d4dce8'}),svgElement('text',{x:20,y:yy+4},'Task '+name));}
  for(let i=0;i<=10;i++){const xx=left+i/10*(width-left-right);svg.append(svgElement('line',{x1:xx,y1:40,x2:xx,y2:440,stroke:'#eef1f6'}),svgElement('text',{x:xx,y:25,'text-anchor':'middle'},(end*i/10).toFixed(end<10?1:0)));}
  svg.append(svgElement('text',{x:width-right,y:463,'text-anchor':'end'},'経過時間 µs（PICO_TRACE us）'));
  for(const link of data.links){const a=data.records[link.request],b=data.records[link.commit];if(!visible.has(a.event)||!visible.has(b.event)||a.us===null||b.us===null)continue;
    const line=svgElement('line',{x1:x(a),y1:y(a),x2:x(b),y2:y(b),stroke:'#65748d','stroke-dasharray':'4 3','data-link':`${a.seq}:${b.seq}`});
    line.append(svgElement('title',{},`request #${a.seq} → HANDOFF_COMMIT #${b.seq}（復元準備完了）`));svg.append(line);}
  for(const r of valid){if(!visible.has(r.event))continue;const xx=x(r),yy=y(r),color=colors[r.event]||'#555';
    const g=svgElement('g',{class:'marker'+(selected===r.index?' selected':''),tabindex:0,role:'button','aria-label':`seq ${r.seq} ${r.event} ${r.us} us`,'data-record':r.index});
    if(r.event==='HANDOFF_COMMIT')g.append(svgElement('path',{d:`M${xx},${yy-7} l7,7 -7,7 -7,-7 Z`,fill:color,stroke:color}));
    else g.append(svgElement('circle',{cx:xx,cy:yy,r:r.event==='TASK_MARK'?8:5,fill:r.event==='TASK_MARK'?color:'white',stroke:color}));
    g.append(svgElement('text',{x:xx,y:yy-12,'text-anchor':'middle',fill:color},r.mark||symbols[r.event]||'?'),svgElement('title',{},`#${r.seq} ${r.us} µs ${r.event} active=${r.active_name}${r.mark?' mark='+r.mark:''}`));
    g.addEventListener('click',()=>selectRecord(r.index));g.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();selectRecord(r.index);}});svg.append(g);}
}
document.getElementById('zoom').addEventListener('input',draw);
document.querySelectorAll('tr[data-index]').forEach(row=>row.addEventListener('click',()=>selectRecord(Number(row.dataset.index))));
draw();
"""


def render(report, source):
    esc = lambda text: html.escape(str(text), quote=True)
    issues = "".join("<li>" + esc(issue) + "</li>" for issue in report["issues"]) or "<li>整合性チェックに不整合はありません。</li>"
    rows = []
    for r in report["records"]:
        cells = [r["seq"], r["us"], r["event"] + (" " + r["mark"] if r["mark"] else "")]
        cells += [r[name + "_name"] + " " + r[name] for name in POINTERS]
        cells += [r["value"], r["source"], r["raw_fields"].get("flags", "?")]
        rows.append(f'<tr id="record-{r["index"]}" data-index="{r["index"]}">' + "".join("<td>" + esc(c) + "</td>" for c in cells) + "</tr>")
    payload = json.dumps(report, ensure_ascii=True).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")
    mappings = " · ".join(f'{t["name"]}: context={t["context"]}, Task={t["task"]}' for t in report["tasks"])
    return f'''<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; base-uri 'none'; form-action 'none'">
<title>Karqri cooperative trace</title><style>{STYLE}</style></head><body><main>
<h1>Karqri · cooperative trace</h1><p class="muted">入力: {esc(source)}<br>run={esc(report['run_id'])} / attempt={esc(report['attempt'])}{' / SIMULATION' if report['simulation'] else ''}</p>
<div class="cards"><div class="card {'good' if report['validation']=='PASS' else 'bad'}">Validation <b>{esc(report['validation'])}</b></div>
<div class="card {'good' if report['trace']=='COMPLETE' else 'bad'}">Trace <b>{esc(report['trace'])}</b></div>
<div class="card">phase: {esc(report['phase'])}<br>{report['count']} records · {report['handoffs']} HANDOFF_COMMIT · overwritten={esc(report['overwritten'])}<br>TASK_MARK: {esc(report['marks'])}</div></div>
<p>{esc(report['validation_reason'])}</p>
<div class="panel"><b>Trace integrity</b><ul>{issues}</ul></div>
<div class="panel"><b>観測の境界</b><p>HANDOFF_COMMITはexception return直前の復元準備完了です。Task命令実行やCPU占有区間を示しません。
点は観測地点、破線はrequest seq対応です。横軸にはfirmwareのusを使用し、Windows受信timestampは使用しません。
同時刻の順序はseqで確認してください。不正時刻のrecordは図から除外し、詳細表には残します。</p><p>{esc(mappings)}</p>
<p>active/pending/subject=context pointer、current=Task pointer。source: 0=unknown、1=yield、2=Task return。
flags: 0x01=H、0x02=suppression、0x04=D、0x08=V、0x10=Handler。</p></div>
<div class="panel"><div id="filters"></div><label>横方向ズーム <input id="zoom" type="range" min="1" max="6" value="1"></label><div id="chart"></div></div>
<div class="panel"><b>選択record</b><pre id="detail">図のマーカーまたは表の行を選択してください。</pre></div>
<div class="panel table-wrap"><table><thead><tr><th>seq</th><th>µs</th><th>event / mark</th><th>active (context)</th><th>current (Task)</th><th>pending (context)</th><th>subject (context)</th><th>value</th><th>source</th><th>flags</th></tr></thead><tbody>{''.join(rows)}</tbody></table></div>
<noscript>JavaScriptが無効です。整合性結果と全record表は参照できます。</noscript>
<script id="trace-data" type="application/json">{payload}</script><script>{SCRIPT}</script>
</main></body></html>'''


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--attempt", type=int, help="select one collector connection attempt")
    args = parser.parse_args(argv)
    try:
        if args.input.resolve() == args.output.resolve():
            raise ValueError("input and output must differ")
        report = read_log(args.input, args.attempt)
        # Never overwrite a prior report or accidentally overwrite the source log.
        with args.output.open("x", encoding="utf-8") as output:
            output.write(render(report, args.input))
        print(f"Validation {report['validation']} / Trace {report['trace']}")
        print(f"{report['count']} records, {report['handoffs']} handoffs, marks={report['marks']}, overwritten={report['overwritten']}")
        for issue in report["issues"]:
            print("-", issue)
        print(args.output.resolve())
        return 0 if report["trace"] == "COMPLETE" else 1
    except (OSError, ValueError, UnicodeError) as exc:
        print("ERROR:", exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
