"""Small numpy subset for ROS 2 generated message helpers on OHOS."""


class _DType:
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return f'numpy.{self.name}'

    def __eq__(self, other):
        return self is other or getattr(other, 'name', None) == self.name


uint8 = _DType('uint8')
float64 = _DType('float64')


class number:
    pass


class ndarray(list):
    def __init__(self, values=(), dtype=None):
        super().__init__(values)
        self.dtype = dtype

    @property
    def size(self):
        return len(self)


def array(values, dtype=None):
    if isinstance(values, ndarray):
        return ndarray(values, dtype=dtype or values.dtype)
    return ndarray(values, dtype=dtype)


def zeros(size, dtype=float64):
    return ndarray([0] * int(size), dtype=dtype)
