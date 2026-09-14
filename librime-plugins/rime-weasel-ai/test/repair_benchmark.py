#!/usr/bin/env python3
"""Test 'select + minimal repair' instead of pure selection.

Motivation (from captured field traffic): for inputs like kuguoyexiaoguo Rime
offers ONE full-length but semantically wrong sentence candidate
("哭过也效果") plus short word candidates. Pure rerank can only pick index 0
and therefore adds nothing. Allowing the model to replace individual
characters *while keeping the character count* enables the fix
("哭过也笑过") without opening the door to hallucinated extra words.
"""
import json
import os
import subprocess
import time

# Configure via env vars, e.g.
#   URL=http://127.0.0.1:11434/v1/chat/completions KEY=ollama \
#   MODELS="qwen2.5:7b" python3 repair_benchmark.py
URL = os.environ.get('URL', 'http://127.0.0.1:8765/v1/chat/completions')
KEY = os.environ.get('KEY', '')
MODELS = os.environ.get('MODELS', 'tmp-test,local-qwen').split(',')

SYS = ("你是中文拼音输入法的候选校准器。给定拼音串和候选列表："
       "1) 若有候选与拼音逐音节完全对应且语义通顺，直接选它，不要改动；"
       "2) 若所有候选都不够通顺，选最接近的一项，并只替换其中个别汉字把它改通顺；"
       "3) 修改后必须与拼音逐音节对应，且**汉字个数与所选候选完全相同**，"
       "不得增加、删除或调换字数；"
       "4) 只输出JSON：{\"index\": 序号, \"text\": \"最终句子\"}，不要解释。")

# (pinyin, candidates, expected_final_text, expects_change, label)
CASES = [
    ("kuguoyexiaoguo",
     ["哭过也效果", "哭过", "苦果", "枯骨", "库", "哭"], "哭过也笑过", True, "需改字-效果/笑过"),
    ("tadediannaobengkuile",
     ["他的电脑崩快了", "他的电脑", "他的", "他"], "他的电脑崩溃了", True, "需改字-崩快/崩溃"),
    ("mingtianwomenyaokaihui",
     ["明天我们要开回", "明天我们", "明天"], "明天我们要开会", True, "需改字-开回/开会"),
    ("zhegewendangxuyaochongxinfanyi",
     ["这个文档需要重新饭一", "这个文档", "这个"], "这个文档需要重新翻译", True, "需改字-饭一/翻译"),
    ("woyijingchiguofanle",
     ["我已经吃过反了", "我已经", "我"], "我已经吃过饭了", True, "需改字-反了/饭了"),
    ("jintiantianqihenbucuo",
     ["今天天气很不粗", "今天天气", "今天"], "今天天气很不错", True, "需改字-不粗/不错"),
    # 候选本来就正确：不得乱改
    ("quanqiufanweidejiezoutiaokongxuyaoyuzhongguohezuo",
     ["全球范围的节奏调控需要与中国合作", "全球范围", "全球"],
     "全球范围的节奏调控需要与中国合作", False, "首选正确-不得改"),
    ("kudoulaibuji",
     ["哭都来不及", "裤兜", "苦斗"], "哭都来不及", False, "首选正确-不得改"),
]


def ask(model, pinyin, cands):
    listing = "\n".join(f"{i}. {c}" for i, c in enumerate(cands))
    user = f"拼音：{pinyin}\n候选：\n{listing}\n请只输出JSON：{{\"index\": 序号, \"text\": \"最终句子\"}}"
    body = {'model': model, 'reasoning_effort': 'none',
            'messages': [{'role': 'system', 'content': SYS},
                         {'role': 'user', 'content': user}],
            'temperature': 0.1, 'max_tokens': 200, 'stream': False}
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
            return None, None, time.time() - t0
        c = d['choices'][0]['message'].get('content') or ''
        data = json.loads(c[c.find('{'):c.rfind('}') + 1])
        return data.get('index'), data.get('text'), time.time() - t0
    except Exception:
        return None, None, time.time() - t0


if __name__ == '__main__':
    print("任务：选择 + 最小修复（字数必须不变）\n")
    for m in MODELS:
        print(f"=== {m} ===")
        ok = 0
        bad_modify = 0
        total_t = 0.0
        for pinyin, cands, expect, needs_change, label in CASES:
            idx, text, dt = ask(m, pinyin, cands)
            total_t += dt
            good = (text == expect)
            ok += good
            if not needs_change and text != expect:
                bad_modify += 1
            mark = '✅' if good else '❌'
            print(f"  {mark} {label:<18} {str(text)[:26]}")
        n = len(CASES)
        print(f"  合计 {ok}/{n}   平均 {total_t/n:.2f}s   误改首选正确项 {bad_modify} 次\n")
