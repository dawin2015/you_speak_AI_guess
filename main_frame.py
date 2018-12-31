# !/usr/bin/python 3.5
# -*- coding = utf-8 -*-

import fastText

from flask import Flask
from flask_script import Manager

app = Flask(__name__)

manager = Manager(app)


@app.route('/')
def index():
    return '<h1>Hello World!</h1>'


@app.route('/user/<name>')
def user(name):
    return '<h1>Hello, %s!</h1>' % name


@app.route('/predict/<txt>')
def predict(txt):
    labels = fastText.load_model('model').predict(txt)
    result, prob = labels
    name = result[0].split('__')[-1]
    return '<h1> %s </h1>' % name


if __name__ == '__main__':
    manager.run()