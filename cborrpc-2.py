"""
================
CBOR-RPC Server
================

How nice is using CBOR for RPC? This is a simple RPC server that uses CBOR for serialization. It allows you to define RPC methods using a decorator and register them with the server. The server will then handle incoming requests and call the appropriate method based on the method name in the request.
It also provides a method to get the interface of the server, which returns a dictionary with the available methods and their arguments.
"""

import asyncio
import cbor2
from typing import Callable, Any, Dict
import functools
import inspect
import struct

class CBORRPCServer:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.methods: Dict[str, Callable] = {}

    def register_method(self, sig, method: Callable):
        for class_name, methods in sig.items():
            for method_name, method_info in methods.items():
                self.methods[f'{class_name}.{method_name}'] = {'method': method,
                                                                'args': method_info['args'],
                                                                'return': method_info['return']}


    async def handle_single_message(self, seq, data, response_queue):
        message = cbor2.loads(data)
        method_name = message['method']
        params = message.get('params', [])
        if method_name == 'get_interface':
            response = self.get_interface()
        elif method_name in self.methods:
            try:
                response = await self.methods[method_name]['method'](*params)
            except Exception as e:
                response = {'error': str(e)}
        else:
            response = {'error': 'Method not found'}

        response = cbor2.dumps(response)
        response_length = struct.pack('!HH', len(response), seq)
        response_message = response_length + response

        await response_queue.put(response_message)

    async def write_responses(self, writer, response_queue):
        while True:
            response_message = await response_queue.get()
            # print(f'Sending response with sequence number {response_message[:4]}')
            writer.write(response_message)
            await writer.drain()
            response_queue.task_done()

    async def handle_client(self, reader, writer):
        # message_queue = asyncio.Queue()
        response_queue = asyncio.Queue()

        writer_task = asyncio.create_task(self.write_responses(writer, response_queue))

        try:
            while True:
                response_length_data = await reader.readexactly(4)
                if len(response_length_data) < 4:
                    raise RuntimeError("Failed to receive the length of the response")

                response_length, seq = struct.unpack('!HH', response_length_data)
                data = await reader.readexactly(response_length)
                asyncio.create_task(self.handle_single_message(seq, data, response_queue))
        except asyncio.IncompleteReadError:
            print("Connection closed")
        except asyncio.CancelledError:
            print("Connection closed")

        writer_task.cancel()
        await response_queue.join()
        writer.close()
        await writer.wait_closed()
        
    def get_interface(self):
        interface = {}
        for method_name, method in self.methods.items():
            class_name, method_name = method_name.split('.')
            if class_name not in interface:
                interface[class_name] = {}
            interface[class_name][method_name] = {"args": method['args'], "return": method['return']}
        return interface

    async def start_server(self):
        server = await asyncio.start_server(self.handle_client, self.host, self.port)
        async with server:
            await server.serve_forever()

"""Decorator to register an RPC method."""
def rpc_method(func: Callable):
    async def wrapper(*args, **kwargs) -> Any:
        return await func(*args, **kwargs)
    func_name = func.__name__
    class_name = func.__qualname__.split('.')[0]

    # Get method arguments and their types
    sig = inspect.signature(func)
    args = [(arg_name, param.annotation.__name__) for arg_name, param in sig.parameters.items() if param.default is param.empty]

    interface = {
        class_name: {
            func_name: {
                'args': args,
                'return': sig.return_annotation.__name__
            }
        }
    }
    wrapper._rpc_method = interface
    return wrapper


class Calculator:
    """
    Calculator RPC Service

    Provides methods to perform basic arithmetic operations.
    """

    @rpc_method
    async def add(self, a: int, b: int) -> int:
        """Add two numbers."""
        # sleep a bit to simulate a slow method
        # await asyncio.sleep(0.1)
        return a + b

    @rpc_method
    async def subtract(self, a: int, b: int) -> int:
        """Subtract b from a."""
        return a - b

    @rpc_method
    async def multiply(self, a: int, b: int) -> int:
        """Multiply two numbers."""
        return a * b

    @rpc_method
    async def divide(self, a: int, b: int) -> float:
        """Divide a by b."""
        if b == 0:
            raise ValueError("Cannot divide by zero")
        return a / b

async def main():
    server = CBORRPCServer('0.0.0.0', 9999)
    calculator = Calculator()

    # Register methods dynamically
    for attr in dir(calculator):
        method = getattr(calculator, attr)
        if hasattr(method, '_rpc_method'):
            server.register_method(method._rpc_method, method)

    await server.start_server()

asyncio.run(main())