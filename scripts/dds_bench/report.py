"""Generate a self-contained HTML report from immutable raw case summaries."""
import html
import json
from pathlib import Path
import sys


def make_report(root):
    results=json.loads((root/'results.json').read_text())
    rows=[]
    plot=[]
    def fmt(x):
        return '—' if x is None else f'{x:.2f}' if isinstance(x,float) else str(x)
    for i,r in enumerate(results):
        c=r['config']
        for name,s in r.get('streams',{}).items():
            if not ('ping_' in name or 'source_' in name): continue
            p=s['rtt_us']
            label=f"{c['rmw']} / {c['qos']} / {c['bytes']//1024} KiB / {c['direction']} / {c['mode']} / {name[-7:]}"
            cells=[i,label,'完成' if r['valid'] else '失败或能力限制',s['attempts'],p['n']]+[p[k] for k in ('p1','p50','p95','p99','max')]+[s['timeouts'],s['publish_errors']]
            rows.append('<tr>'+''.join('<td>'+html.escape(fmt(x))+'</td>' for x in cells)+'</tr>')
            if p['p99'] is not None: plot.append((label,p['p50'],p['p99']))
    bars=[]
    maximum=max((x[2] for x in plot),default=1) or 1
    for i,(label,p50,p99) in enumerate(plot):
        y=25+i*48
        bars.append(f'<text x="8" y="{y}" font-size="11">{html.escape(label)}</text><rect x="530" y="{y-12}" width="{400*p99/maximum}" height="12" fill="#b9cdfb"/><rect x="530" y="{y-12}" width="{400*p50/maximum}" height="12" fill="#2359bb"/><text x="530" y="{y+15}" font-size="11">p50 {p50:.2f} / p99 {p99:.2f} μs</text>')
    chart=f'<svg viewBox="0 0 960 {max(60,len(plot)*48+20)}">'+''.join(bars)+'</svg>'
    out='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>DDS 双板测试报告</title>
<style>body{font:15px system-ui;max-width:1500px;margin:40px auto;padding:0 24px;color:#17233a}table{border-collapse:collapse;width:100%;font-size:13px}th,td{border-bottom:1px solid #dde3ec;padding:10px;text-align:left}th{background:#edf2fa}svg{width:100%;max-width:1200px}.note{background:#f0f4fa;padding:18px}details{margin:22px 0}pre{overflow:auto;font-size:12px}</style>
<h1>DDS 双板测试报告</h1><div class="note">RTT 使用发送板单调时钟，单位 μs；包含回显端完整性校验。统计采用 nearest-rank。超时和异常不计入成功样本的延迟分位数，单独列出。样本少于 10,000 时，p1/p99 仅供工具验证；本报告不能自动等同于正式性能排名。深色为 p50，浅色为 p99。</div>'''
    out+=chart+'<h2>逐项结果</h2><table><tr>'+''.join('<th>'+x+'</th>' for x in ['编号','配置','结果','尝试','RTT样本','p1','p50','p95','p99','最大','超时','发送异常'])+'</tr>'+''.join(rows)+'</table>'
    policies=sorted({r['config'].get('network_policy','auto_interface_development_baseline') for r in results})
    out+='<p>本批网口策略：'+html.escape(', '.join(policies))+'。auto_interface 批次可能包含 Fast DDS 有线/Wi-Fi 重复发包，不能和固定 eth1 批次混合排名。</p>'
    out+='<h2>吞吐、CPU、内存、接口与恢复详情</h2><p>吞吐为接收端首末有效到达窗口中的有效载荷速度；不等于物理线路速率。接口计数包含其他系统流量。CPU 100% 表示一个 CPU 核；内存为应用的 RSS 采样峰值，采样间隔 250 ms。</p>'
    for i,r in enumerate(results):
        out+='<details><summary>Case '+str(i)+' 详情</summary><pre>'+html.escape(json.dumps(r,ensure_ascii=False,indent=2))+'</pre></details>'
    (root/'report.html').write_text(out+'</html>',encoding='utf-8')
    return root/'report.html'


if __name__=='__main__':
    print(make_report(Path(sys.argv[1])))
