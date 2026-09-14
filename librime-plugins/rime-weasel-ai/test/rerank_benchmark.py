#!/usr/bin/env python3
"""Strict validation of the plugin's REAL rerank protocol.

Reproduces exactly what weasel_ai_correction_service.cc sends in rerank mode
(same system prompt, same "N. text" candidate numbering, same index
validation) and measures accuracy, false-change rate (picking a wrong index
when the first candidate was already correct) and latency over 14 cases x
2 rounds.

Uses the exact system prompt, candidate numbering format and index validation
that weasel_ai_correction_service.cc uses, so the result predicts field
behaviour. Measures:
  * accuracy  - picked the intended candidate
  * false-change rate - picked a WRONG index when the first candidate was
    already correct (this would inject a bad AI candidate in production)
  * latency
"""
import json
import subprocess
import time

# Endpoint/model are configurable so this can be re-run against any
# OpenAI-compatible service (including a local one).
#
#   RERANK_URL=http://127.0.0.1:11434/v1/chat/completions \
#   RERANK_KEY=ollama \
#   RERANK_MODELS="qwen2.5:7b" python3 rerank_benchmark.py
import os

URL = os.environ.get('RERANK_URL',
                     'http://127.0.0.1:8765/v1/chat/completions')
KEY = os.environ.get('RERANK_KEY', '')

# verbatim from kDefaultRerankPrompt
SYS = ("你是中文拼音输入法的候选重排器。给定拼音串和候选列表，选出与拼音逐音节"
       "对应、整句语义最通顺的一项。规则："
       "1) 只能从候选列表中选择，不得改写、拼接或新增文字；"
       "2) 语义通顺优先于词语常见程度；"
       "3) 只输出JSON：{\"index\": 序号}（序号从0开始），不要解释。")

# (pinyin, candidates, expected_index, label)
CASES = [
    ("weishenmebajiaozhundehouxuancifangzainamekaohou",
     ["为什么芭蕉准的候选次方在那么靠后", "为什么把校准的候选词放在那么靠后", "为什么；喂什么"], 1, "难-抛弃首位"),
    ("tadediannaobengkuile",
     ["他的电脑崩快了", "他的电脑崩溃了", "他的电脑", "他的"], 1, "同音-崩快"),
    ("xiadaluandayeshikeyijiaozhunde",
     ["下达乱打也是可以校准的", "瞎打乱打也是可以校准的", "下达；瞎打"], 1, "语义不通"),
    ("mingtianwomenyaokaihui",
     ["明天我们要开回", "明天我们要开会", "明天我们"], 1, "同音-开回"),
    ("zhegewendangxuyaochongxinfanyi",
     ["这个文档需要重新饭一", "这个文档需要重新翻译", "这个文档"], 1, "同音-饭一"),
    ("kudoulaibuji",
     ["裤兜来不及", "哭都来不及", "苦斗来不及"], 1, "成语"),
    ("zhegeshiqinghendanteng",
     ["这个事情很但疼", "这个事情很蛋疼", "这个事情"], 1, "语义-但疼"),
    ("jintiantianqihenbucuo",
     ["今天天气很不粗", "今天天气很不错", "今天天气"], 1, "同音-不粗"),
    ("woyijingchiguofanle",
     ["我已经吃过反了", "我已经吃过饭了", "我已经"], 1, "同音-反了"),
    ("qingbangwokaiyixiafangjian",
     ["请帮我开以下房间", "请帮我开一下房间", "请帮我"], 1, "同音-以下/一下"),
    # 第一候选本来就正确：模型应返回 0（不得乱改）
    ("quanqiufanweidejiezoutiaokongxuyaoyuzhongguohezuo",
     ["全球范围的节奏调控需要与中国合作", "全球范围", "全球"], 0, "首选正确-长句"),
    ("wolaikankanhouxuancizainali",
     ["我来看看候选词在哪里", "我来看看", "我来"], 0, "首选正确-常规"),
    ("woxiwangnenggouzaizhoumowanquanwanchengzhegexiangmu",
     ["我希望能够在周末完全完成这个项目", "我希望", "我希望能够"], 0, "首选正确-长句"),
    ("tashuozhegexiangmubixujintianwancheng",
     ["他说这个项目必须今天完成", "他说", "他说这个"], 0, "首选正确-常规"),
]

MODELS = os.environ.get('RERANK_MODELS', 'qwen2.5:7b').split(',')
ROUNDS = 2


def ask(model, pinyin, cands):
    listing = "\n".join(f"{i}. {c}" for i, c in enumerate(cands))
    user = f"拼音：{pinyin}\n候选：\n{listing}\n请只输出JSON：{{\"index\": 序号}}"
    body = {'model': model, 'reasoning_effort': 'none',
            'messages': [{'role': 'system', 'content': SYS},
                         {'role': 'user', 'content': user}],
            'temperature': 0.1, 'max_tokens': 128, 'stream': False}
    t0 = time.time()
    try:
        out = subprocess.run(
            ['curl', '-s', '-m', '60', '-X', 'POST', URL,
             '-H', 'Content-Type: application/json',
             '-H', f'Authorization: Bearer {KEY}',
             '--data-binary', json.dumps(body, ensure_ascii=False)],
            capture_output=True, text=True, timeout=70).stdout
        d = json.loads(out)
        if 'error' in d:
            return None, time.time() - t0
        c = d['choices'][0]['message'].get('content') or ''
        data = json.loads(c[c.find('{'):c.rfind('}') + 1])
        return int(data.get('index', -1)), time.time() - t0
    except Exception:
        return None, time.time() - t0


if __name__ == '__main__':
    stats = {m: {'ok': 0, 'n': 0, 't': 0.0, 'false_change': 0,
                 'false_change_cases': []} for m in MODELS}
    detail = []
    for pinyin, cands, expect, label in CASES:
        row = {'label': label, 'expect': expect, 'picks': {}}
        for m in MODELS:
            picks = []
            for _ in range(ROUNDS):
                idx, dt = ask(m, pinyin, cands)
                st = stats[m]
                st['n'] += 1
                st['t'] += dt
                picks.append(idx)
                if idx == expect:
                    st['ok'] += 1
                if expect == 0 and idx not in (0, None):
                    st['false_change'] += 1
                    st['false_change_cases'].append(f"{label}→{idx}")
            row['picks'][m] = picks
        detail.append(row)

    print("逐用例（每次取 2 轮结果）")
    print(f"{'用例':<18}", end='')
    for m in MODELS:
        print(f"{m:<20}", end='')
    print()
    for row in detail:
        print(f"{row['label']:<18}", end='')
        for m in MODELS:
            picks = row['picks'][m]
            marks = ''.join('✅' if p == row['expect'] else ('❌' if p is not None else '⚠') for p in picks)
            print(f"{marks} {str(picks):<14}", end='')
        print()
    print()
    for m in MODELS:
        st = stats[m]
        print(f"{m:<14} 准确 {st['ok']}/{st['n']}  平均 {st['t']/st['n']:.2f}s  "
              f"误改(首选正确却改) {st['false_change']} 次")
        if st['false_change_cases']:
            print(f"{'':<14} 误改样例: {st['false_change_cases'][:4]}")
