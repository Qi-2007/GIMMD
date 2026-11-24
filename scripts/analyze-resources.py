"""Summarize bounded runtime resource surveys without printing the scene hierarchy."""
import argparse
import json
from pathlib import Path


def summarize(report):
    found = []
    def visit(value):
        if isinstance(value, dict):
            if 'resourceLookupStatus' in value:
                found.append(value)
            else:
                for item in value.values():
                    visit(item)
        elif isinstance(value, list):
            for item in value:
                visit(item)
    visit(report)
    if len(found) != 1:
        raise ValueError('Expected exactly one resource survey')
    survey = found[0]
    return {
        'schemaVersion': 1,
        'sourcePid': report.get('pid'),
        'executableSha256': report.get('executableSha256'),
        'engineException': report.get('engineException'),
        'entryBytesRestored': report.get('entryBytesRestored'),
        'settersCalled': report.get('settersCalled'),
        'status': survey['resourceLookupStatus'],
        'classes': survey.get('resourceClasses', []),
        'bundles': survey.get('bundleEnumeration', {}),
        'prefabs': survey.get('prefabEnumeration', {}),
        'textAssets': survey.get('textAssetEnumeration', {}),
        'methods': survey.get('methods', []),
        'trace': report.get('resourceTrace', {}),
        'assetLinks': survey.get('assetLinks', {}),
        'managerDirectory': survey.get('managerDirectory', {}),
        'loadedPackages': survey.get('loadedPackages', {}),
        'scriptMetadataFields': survey.get('scriptMetadataFields', []),
        'note': 'Method addresses are discovery evidence; loading ABI and resource dependencies remain unverified.'
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = summarize(json.loads(args.report.read_text(encoding='utf-8-sig')))
    data = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        args.output.write_text(data, encoding='utf-8')
    else:
        print(data, end='')
