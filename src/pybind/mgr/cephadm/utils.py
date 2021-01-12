import logging
import json
import socket
from enum import Enum
from functools import wraps
from typing import Callable, TypeVar, List, NewType, TYPE_CHECKING, Any
from orchestrator import OrchestratorError

if TYPE_CHECKING:
    from cephadm import CephadmOrchestrator

T = TypeVar('T')
logger = logging.getLogger(__name__)

ConfEntity = NewType('ConfEntity', str)


class CephadmNoImage(Enum):
    token = 1


# Used for _run_cephadm used for check-host etc that don't require an --image parameter
cephadmNoImage = CephadmNoImage.token


def name_to_config_section(name: str) -> ConfEntity:
    """
    Map from daemon names to ceph entity names (as seen in config)
    """
    daemon_type = name.split('.', 1)[0]
    if daemon_type in ['rgw', 'rbd-mirror', 'nfs', 'crash', 'iscsi']:
        return ConfEntity('client.' + name)
    elif daemon_type in ['mon', 'osd', 'mds', 'mgr', 'client']:
        return ConfEntity(name)
    else:
        return ConfEntity('mon')


def forall_hosts(f: Callable[..., T]) -> Callable[..., List[T]]:
    @wraps(f)
    def forall_hosts_wrapper(*args: Any) -> List[T]:
        from cephadm.module import CephadmOrchestrator

        # Some weired logic to make calling functions with multiple arguments work.
        if len(args) == 1:
            vals = args[0]
            self = None
        elif len(args) == 2:
            self, vals = args
        else:
            assert 'either f([...]) or self.f([...])'

        def do_work(arg: Any) -> T:
            if not isinstance(arg, tuple):
                arg = (arg, )
            try:
                if self:
                    return f(self, *arg)
                return f(*arg)
            except Exception as e:
                logger.exception(f'executing {f.__name__}({args}) failed.')
                raise

        assert CephadmOrchestrator.instance is not None
        return CephadmOrchestrator.instance._worker_pool.map(do_work, vals)

    return forall_hosts_wrapper


def get_cluster_health(mgr: 'CephadmOrchestrator') -> str:
    # check cluster health
    ret, out, err = mgr.check_mon_command({
        'prefix': 'health',
        'format': 'json',
    })
    try:
        j = json.loads(out)
    except ValueError:
        msg = 'Failed to parse health status: Cannot decode JSON'
        logger.exception('%s: \'%s\'' % (msg, out))
        raise OrchestratorError('failed to parse health status')

    return j['status']


def is_repo_digest(image_name: str) -> bool:
    """
    repo digest are something like "ceph/ceph@sha256:blablabla"
    """
    return '@' in image_name


def resolve_ip(hostname: str) -> str:
    try:
        return socket.getaddrinfo(hostname, None, flags=socket.AI_CANONNAME, type=socket.SOCK_STREAM)[0][4][0]
    except socket.gaierror as e:
        raise OrchestratorError(f"Cannot resolve ip for host {hostname}: {e}")
