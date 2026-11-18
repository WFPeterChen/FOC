# -*- coding: utf-8 -*-
"""
整理 FalconFOC 主驱一体板 BOM：把导出 BOM 中分散到多行的同种元件
按 "种类" 合并，精确给出每种元件的数量、位号、封装、料号。
不依赖 openpyxl，直接解析 xlsx(zip+xml)。
"""
import zipfile, xml.etree.ElementTree as ET, re, csv, os
from collections import OrderedDict

NS = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'BOM_主驱一体_V1.2（有源晶振）_PCB5_3_2026-06-21.xlsx')


def col_to_idx(ref):
    m = re.match(r'([A-Z]+)(\d+)', ref)
    letters = m.group(1)
    c = 0
    for ch in letters:
        c = c * 26 + (ord(ch) - ord('A') + 1)
    return c - 1


def read_sheet(path):
    z = zipfile.ZipFile(path)
    shared = []
    if 'xl/sharedStrings.xml' in z.namelist():
        root = ET.fromstring(z.read('xl/sharedStrings.xml'))
        for si in root.findall(f'{NS}si'):
            shared.append(''.join(t.text or '' for t in si.iter(f'{NS}t')))
    root = ET.fromstring(z.read('xl/worksheets/sheet1.xml'))
    rows = []
    for row in root.iter(f'{NS}row'):
        cells, maxc = {}, -1
        for c in row.findall(f'{NS}c'):
            ci = col_to_idx(c.get('r'))
            t = c.get('t')
            v = c.find(f'{NS}v')
            isn = c.find(f'{NS}is')
            if t == 's' and v is not None:
                val = shared[int(v.text)]
            elif isn is not None:
                val = ''.join(x.text or '' for x in isn.iter(f'{NS}t'))
            elif v is not None:
                val = v.text
            else:
                val = ''
            cells[ci] = val
            maxc = max(maxc, ci)
        rows.append([cells.get(i, '') for i in range(maxc + 1)])
    return rows


def category(designator):
    """根据位号首字母判断元件大类"""
    p = re.match(r'[A-Za-z]+', designator or '')
    pre = p.group(0).upper() if p else ''
    return {
        'C': '电容', 'R': '电阻', 'L': '电感', 'D': '二极管',
        'Q': 'MOS管', 'U': 'IC/模块', 'CN': '连接器', 'LED': 'LED',
        'NTC': '热敏电阻', 'SW': '按键', 'X': '晶振',
        'USBC': '连接器', 'TP': '安装孔',
    }.get(pre, pre)


def norm_value(comment):
    """归一化数值标注：100nf->100nF, 大小写统一"""
    s = (comment or '').strip()
    s = s.replace('nf', 'nF').replace('uf', 'uF').replace('pf', 'pF')
    s = s.replace('UF', 'uF').replace('NF', 'nF')
    return s


def canon_fp(fp):
    """归一化封装用于合并：同一物理封装在源 BOM 里大小写/后缀不一致
    (R0603_S / R0603_SMALL / r0603_small 视为同一 0603)。"""
    u = (fp or '').upper()
    m = re.match(r'([RC])0603', u)
    if m:
        return m.group(1) + '0603'
    m = re.match(r'(C0805|C1206)', u)
    if m:
        return m.group(1)
    return u


# 每个归一化封装的规范显示名
FP_DISPLAY = {'R0603': 'R0603_SMALL', 'C0603': 'C0603_SMALL',
              'C0805': 'C0805_SMALL', 'C1206': 'C1206_SMALL'}


def sort_des_key(d):
    m = re.match(r'([A-Za-z]+)(\d+)', d)
    return (m.group(1), int(m.group(2))) if m else (d, 0)


# 经原理图核对，导出 BOM 中料号填错的器件（值正确、料号错）。
# key=位号，apply 时覆盖该行的厂商料号/立创编号，并给该组打备注。
CORRECTIONS = {
    'C5': dict(mp='0603B104K500NT', sp='C30926',
               note='原BOM料号CL10A106(实为10µF)有误；原理图为100nF，已按100nF改正'),
    'C6': dict(mp='0603B104K500NT', sp='C30926',
               note='原BOM料号CL10A106(实为10µF)有误；原理图为100nF，已按100nF改正'),
    'R1': dict(mp='0603WAF2003T5E', sp='',
               note='原BOM料号RMC0603 52K3(实为52.3kΩ)有误；原理图为200kΩ(12V升压反馈)，'
                    '已按200kΩ改正，立创请搜 200kΩ 0603 1%'),
    # 选型变更：用户决定母线 TVS 改用 SMBJ30CA（钳位48.4V<电容50V，
    # 给6S更多裕量且无需升级电容）。覆盖 val 使分组按新料号显示。
    'D2': dict(val='SMBJ30CA', mp='SMBJ30CA', sp='C153363',
               note='原板为SMBJ28CA(钳位45.4V)；改用SMBJ30CA(VRWM30V/钳位48.4V<电容50V)，'
                    '为6S供电增加裕量，其它元件无需改动'),
}


def main():
    rows = read_sheet(SRC)
    header = rows[0]
    col = {name: i for i, name in enumerate(header)}
    iQty, iCom, iDes = col['Quantity'], col['Comment'], col['Designator']
    iFp, iMp, iSp = col['Footprint'], col['Manufacturer Part'], col['Supplier Part']

    groups = OrderedDict()  # key=(category, value, footprint_norm)
    for r in rows[1:]:
        if len(r) <= iDes or not r[iDes]:
            continue
        des_list = [d.strip() for d in r[iDes].split(',') if d.strip()]
        cat = category(des_list[0]) if des_list else ''
        val = norm_value(r[iCom])
        for d in des_list:  # 选型变更可改写显示值（如 D2 的 TVS 型号）
            if d in CORRECTIONS and CORRECTIONS[d].get('val'):
                val = CORRECTIONS[d]['val']
        cfp = canon_fp(r[iFp])
        key = (cat, val, cfp)
        g = groups.setdefault(key, {
            'cat': cat, 'val': val, 'fp': FP_DISPLAY.get(cfp, r[iFp]),
            'des': [], 'mp': set(), 'sp': set(), 'note': set()})
        g['des'] += des_list
        # 料号：优先用核对后的校正值
        mp, sp = r[iMp], (r[iSp] if len(r) > iSp else '')
        for d in des_list:
            if d in CORRECTIONS:
                c = CORRECTIONS[d]
                mp, sp = c['mp'], c['sp']
                g['note'].add(c['note'])
        if mp:
            g['mp'].add(mp)
        if sp:
            g['sp'].add(sp)

    # 输出
    out_rows = []
    for g in groups.values():
        des = sorted(g['des'], key=sort_des_key)
        out_rows.append({
            '类别': g['cat'],
            '规格/值': g['val'],
            '封装': g['fp'],
            '数量': len(des),
            '位号': ','.join(des),
            '制造商料号': ' / '.join(sorted(g['mp'])),
            '立创编号': ' / '.join(sorted(g['sp'])),
            '备注/校正': ' ; '.join(sorted(g['note'])),
        })

    # 排序：先按类别，再按数量降序
    cat_order = ['电阻', '电容', '电感', '二极管', 'MOS管', 'IC/模块',
                 '晶振', 'LED', '热敏电阻', '连接器', '按键', '安装孔']
    out_rows.sort(key=lambda x: (cat_order.index(x['类别'])
                  if x['类别'] in cat_order else 99, -x['数量'], x['规格/值']))

    out_csv = os.path.join(HERE, 'BOM_整理_按元件种类.csv')
    fields = ['类别', '规格/值', '封装', '数量', '位号', '制造商料号', '立创编号', '备注/校正']
    with open(out_csv, 'w', newline='', encoding='utf-8-sig') as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(out_rows)

    # 控制台打印
    total_by_cat = {}
    print(f"{'类别':<8}{'规格/值':<14}{'封装':<20}{'数量':>4}  位号")
    print('-' * 100)
    for r in out_rows:
        total_by_cat[r['类别']] = total_by_cat.get(r['类别'], 0) + r['数量']
        print(f"{r['类别']:<8}{r['规格/值']:<14}{r['封装']:<20}{r['数量']:>4}  {r['位号']}")
    print('-' * 100)
    print('各类别合计：', ', '.join(f"{k}={v}" for k, v in total_by_cat.items()))
    print('元件种类数：', len(out_rows), ' 总贴装数：', sum(r['数量'] for r in out_rows))
    print('已生成：', out_csv)


if __name__ == '__main__':
    main()
