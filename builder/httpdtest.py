#!/usr/bin/env python3
import sys, os, logging, time, argparse
import requests, json

import importlib.util, asyncio
import tornado

logging.basicConfig(format=f"[%(asctime)s][%(levelname)s][%(name)s][%(funcName)s][#%(lineno)d]%(message)s")

logger = logging.getLogger()
logger_level = logging.DEBUG
logger.setLevel(logger_level)

pref = {
    "req_timeout": 3
}

def logger_level_verbose(inc = 1, lvl_base = None):
    if lvl_base is None:
        global logger_level
        lvl_base = logger_level
    lut = [logging.CRITICAL, logging.ERROR, logging.WARNING, logging.INFO,
            logging.DEBUG, logging.NOTSET]
    idx = lut.index(lvl_base)
    if inc + idx >= len(lut):
        return lut[-1]
    if inc + idx < 0:
        return lut[0]
    return lut[inc + idx]

def fread(fn):
    with open(fn, "r") as f:
        return f.read()

def testReq1(method="get", url="http://localhost:8766/", 
        pld={"Timestamp": int(time.time())}):
    try:
        func = getattr(requests,str(method))
        resp = func(url, json=pld, timeout=pref["req_timeout"])
        logger.debug(f"result: {resp.content}")
        return resp
    except Exception as err:
        logger.error(f"response {err}")
        return
    return

class DocrootHandler(tornado.web.RequestHandler):

    def get(self):
        logger.debug(f"recv GET {self.request.path}\n{self.request.body}")
        self.write({
            "status": True, 
            "result": {
                "message": "Hello, world"
            }
        })

    def post(self):
        logger.debug(f"recv POST {self.request.path}\n{self.request.body}")
        self.write("Hello, world")

async def main(argv):
    argparser = argparse.ArgumentParser()
    argparser.add_argument("-p", "--port", help="Port to listen on")
    args = argparser.parse_args(argv[1:])

    loop = asyncio.get_event_loop()

    if args.port:
        port = int(args.port)
    else:
       port = 8766

    webapp = tornado.web.Application([
        (r"/", DocrootHandler),
        (r"/SetMQTTBlockerSetting", DocrootHandler),
    ])
    webapp.listen(port)

    await asyncio.gather(
        loop.run_in_executor(None, lambda: testReq1(
                url=f"http://localhost:{port}/", pld={
                    "Timestamp": int(time.time())
                })),

        asyncio.sleep(pref["req_timeout"]),
    )
    logger.debug("finally")

if __name__ == '__main__':
    asyncio.run(main(sys.argv))
