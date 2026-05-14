>>>>>>>>>>main>>>>INCLUDE_MULTI_CPU
from .builder import UiDocumentBuilder
from .rpc import ElaraUiRpcClient, ElaraUiRpcError
@include [%INCLUDE_MULTI_CPU% == 1] __init__.py.multi_cpu_import>>>>

__all__ = [
    "UiDocumentBuilder",
    "ElaraUiRpcClient",
    "ElaraUiRpcError",
@include [%INCLUDE_MULTI_CPU% == 1] __init__.py.multi_cpu_all>>>>
]
<<<<<<<<<<main

>>>>>>>>>>multi_cpu_import
try:
    from .multi_cpu import MultiCpuWorkerTemplate, ensure_multi_cpu_runtime
except RuntimeError:
    MultiCpuWorkerTemplate = None
    ensure_multi_cpu_runtime = None
<<<<<<<<<<multi_cpu_import

>>>>>>>>>>multi_cpu_all
    "MultiCpuWorkerTemplate",
    "ensure_multi_cpu_runtime",
<<<<<<<<<<multi_cpu_all
