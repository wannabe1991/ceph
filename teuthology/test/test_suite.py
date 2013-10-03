import textwrap
from .. import suite
from .fake_archive import FakeArchive


class TestResultsEmail(object):
    reference = {
        'name': 'test_name',
        'jobs': [
            {'info': {'description': 'description for job with name test_name',
                      'job_id': 30481,
                      'name': 'test_name',
                      'owner': 'job@owner',
                      'pid': 80399},
             'job_id': 30481},
            {'info': {'description': 'description for job with name test_name',
                      'job_id': 88979,
                      'name': 'test_name',
                      'owner': 'job@owner',
                      'pid': 3903},
                'job_id': 88979,
                'summary': {
                    'description': 'description for job with name test_name',
                    'duration': 35190, 'failure_reason': 'Failure reason!',
                    'owner': 'job@owner',
                    'success': False}},
            {'info': {'description': 'description for job with name test_name',
                      'job_id': 68369,
                      'name': 'test_name',
                      'owner': 'job@owner',
                      'pid': 38524},
             'job_id': 68369,
             'summary': {
                 'description': 'description for job with name test_name',
                 'duration': 33771, 'owner': 'job@owner', 'success':
                 True}},
        ],
        'subject': '1 failed, 1 hung, 1 passed in test_name',
        'body': textwrap.dedent("""
    Test Run: test_name
    =================================================================
    logs:   http://qa-proxy.ceph.com/teuthology/test_archive/
    failed: 1
    hung:   1
    passed: 1

    Failed
    =================================================================
    [88979]  description for job with name test_name
    -----------------------------------------------------------------
    time:   35190s
    log:    http://qa-proxy.ceph.com/teuthology/test_archive/88979/

        Failure reason!


    Hung
    =================================================================
    [30481] description for job with name test_name

    Passed
    =================================================================
    [68369] description for job with name test_name
    time:    33771s
    """).strip(),
    }

    def setup(self):
        self.archive = FakeArchive()
        self.archive.setup()

    def teardown(self):
        self.archive.teardown()

    def test_build_email_body(self):
        self.archive.populate_archive(self.reference['jobs'])
        (subject, body) = suite.build_email_body(
            self.reference['name'],
            self.archive.archive_base, 36000)
        assert subject == self.reference['subject']
        assert body == self.reference['body']
