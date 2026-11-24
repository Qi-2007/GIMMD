"""Correlate passive preview trace events by native identity; never infer ownership from timing alone."""
import argparse
import json
from collections import Counter
from pathlib import Path


def summarize(report):
    trace = report.get('trace', report.get('resourceTrace', {}))
    rows = trace.get('pipelineRecords', [])
    safety = {
        'engineException': report.get('engineException'),
        'entryBytesRestored': report.get('entryBytesRestored'),
        'traceEntriesRestored': trace.get('entryRestored'),
        'activeCallbacks': trace.get('activeCallbacks'),
    }
    if safety != dict(engineException=0, entryBytesRestored=True,
                      traceEntriesRestored=True, activeCallbacks=0):
        raise ValueError('Trace did not confirm clean completion and hook restoration')
    chains = []
    for cache in rows:
        if cache['kind'] != 'instance-cache' or not cache['result']:
            continue
        cache_id = cache['result']
        files = [r for r in rows if r['kind'] in ('bundle-file', 'mhy-bundle-file')
                 and r['result'] == cache['object'] and r['endTick'] <= cache['startTick']]
        loads = [r for r in rows if r['kind'] in ('load-template','load-template-async')
                 and r['object'] == cache_id and r['startTick'] >= cache['endTick']]
        instances = [r for r in rows if r['kind'] == 'instance'
                     and r['object'] == cache_id and r['startTick'] >= cache['endTick']]
        spawners = {r['arg2'] for r in loads if r['kind']=='load-template-async' and r['arg2']}
        ready = [r for r in rows if r['kind']=='template-ready' and r['object'] in spawners]
        data_ids = {r['templateData'] for r in loads + ready if r['templateData']}
        finalizations = [r for r in rows if r['kind']=='template-finalize' and r['templateData'] in data_ids]
        providers = [r for r in rows if r['kind']=='role-provider'
                     and r['arg1']==cache['arg1'] and r['arg2']==cache['object']
                     and r['startTick']<=cache['startTick']<=r['endTick']]
        clones = [r for r in rows if r['kind'] == 'template-clone' and r['templateData'] in data_ids]
        chains.append({
            'hash': cache['arg1'], 'hashHex': f"0x{cache['arg1']:08X}",
            'bundle': cache['objectName'], 'cacheThread': cache['thread'],
            'callerRvas': [hex(v) for v in cache['callerRvas']],
            'fileLoads': [{'path': r['path'], 'offset': r['arg1'],
                           'option1': r['option1'], 'option2': r['option2'],
                           'bundle': r['resultName']} for r in files],
            'providers': [{'class': r['objectName'], 'handle': r['option1']} for r in providers],
            'templateLoads': len(loads), 'templateReady': len(ready), 'templateClones': len(clones),
            'finalizedModels': [r['resultName'] for r in finalizations],
            'instances': [r['resultName'] for r in instances],
        })
    return {
        'schemaVersion': 1, 'safety': safety,
        'pipelineHooks': trace.get('pipelineHooks', []),
        'counts': dict(Counter(r['kind'] for r in rows)),
        'bufferTruncated': trace.get('pipelineCalls', 0) > trace.get('pipelineCapacity', 0),
        'cacheChains': chains,
        'unlinkedAsyncClones': [
            {'templateData': r['templateData'], 'objects': r['arg1'],
             'thread': r['thread'], 'callerRvas': [hex(v) for v in r['callerRvas']]}
            for r in rows if r['kind'] == 'template-clone'
            and not any(r['templateData'] == t['templateData']
                        for t in rows if t['kind'] == 'load-template')
        ],
        'note': 'Same native identities and event order establish the links. Missing file/template events can reflect prior caching or unhooked asynchronous paths. option2 semantics are not yet fully verified.',
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = summarize(json.loads(args.report.read_text(encoding='utf-8-sig')))
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
