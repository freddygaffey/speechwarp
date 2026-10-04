"""A rule-based syllable count of English text, the reference for the syllable counter."""
import re
def syl(w):
    """Syllables in an English word, by rule: vowel groups, less silent endings. Within a few percent on prose."""
    w = re.sub(r'[^a-z]', '', w.lower())
    if not w: return 0
    n = len(re.findall(r'[aeiouy]+', w))
    if w.endswith('e') and not w.endswith(('le', 'ee', 'ye')) and n > 1: n -= 1
    elif w.endswith('le') and len(w) > 2 and w[-3] in 'aeiouy' and n > 1: n -= 1
    if w.endswith('ed') and not w.endswith(('ted', 'ded')) and n > 1: n -= 1
    if w.endswith('es') and not re.search(r'(s|x|z|ch|sh|ge|ce)es$', w) and n > 1: n -= 1
    return max(1, n)
def count(text): return sum(syl(w) for w in re.findall(r"[A-Za-z']+", text))
if __name__ == '__main__':
    print(count("The lighthouse keeper had not spoken to anyone for eleven days when the supply boat finally rounded the point."), "(hand count 30)")
    print(count(open('passage.txt').read()), "in the passage")
