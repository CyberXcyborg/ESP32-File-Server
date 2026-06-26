import re, subprocess

# Compare base vs current to confirm changes are balanced
base = subprocess.check_output(['git', 'show', 'HEAD~3:ESP32_File_Server.ino']).decode()
curr = open('ESP32_File_Server.ino').read()

def clean_count(content):
    # Remove R"rawliteral(...) rawliteral" - non-greedy match
    content = re.sub(r'R"rawliteral\(.*?\)rawliteral"', '', content, flags=re.DOTALL)
    # Remove remaining string literals  
    content = re.sub(r'"(?:[^"\\]|\\.)*"', '', content)
    content = re.sub(r"'(?:[^'\\]|\\.)*'", '', content)
    # Remove comments
    content = re.sub(r'//[^\n]*', '', content)
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    return content.count('{'), content.count('}')

bo, bc = clean_count(base)
co, cc = clean_count(curr)
delta_open = co - bo
delta_close = cc - bc
print(f"Base:  {bo} open, {bc} close (diff={bo-bc})")
print(f"Curr:  {co} open, {cc} close (diff={co-cc})")
print(f"Delta: +{delta_open} open, +{delta_close} close (net change: {delta_open - delta_close})")
if delta_open == delta_close:
    print("PASS: Changes are balanced")
else:
    print("FAIL: Changes are unbalanced")
    exit(1)
