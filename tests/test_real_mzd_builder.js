#!/usr/bin/env node
'use strict'
// Exercise the actual addTweak and writeTweaksFile functions from MZD-AIO,
// with a minimal replacement for the legacy appender stream dependency.
const fs = require('fs')
const path = require('path')
const vm = require('vm')
const assert = require('assert')
const os = require('os')
const { Readable } = require('stream')

const root = path.resolve(__dirname, '..')
const source = fs.readFileSync(path.join(root, 'app/assets/js/build-tweaks.js'), 'utf8')
const start = source.indexOf('function addTweak (twk) {')
const end = source.indexOf('function langVar (id) {', start)
assert(start >= 0 && end > start, 'Cannot locate actual MZD-AIO builder functions')
const code = source.slice(start, end)

class Appender extends Readable {
  constructor(files) {
    super()
    this.files = files
    this.sent = false
  }
  _read() {
    if (this.sent) return
    this.sent = true
    for (const file of this.files) this.push(fs.readFileSync(file))
    this.push(null)
  }
}

async function run(mode) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mazda-aio-real-builder-'))
  const builddir = path.join(root, 'app/files/tweaks') + '/'
  const tweaks2write = [builddir + '00_intro.txt', builddir + '00_start.txt']
  const sandbox = {
    fs, builddir, tmpdir: dir, tweaks2write, appender: Appender,
    aioLog: () => {},
    convert2LF: () => {}
  }
  vm.createContext(sandbox)
  vm.runInContext(code, sandbox, { filename: 'build-tweaks.js' })
  const fragment = mode === 'install' ? '29_factoryaatouch-i.txt' : '29_factoryaatouch-u.txt'
  sandbox.addTweak(fragment)
  sandbox.addTweak('00_end.txt')
  sandbox.writeTweaksFile()
  const output = path.join(dir, 'tweaks.txt')
  await new Promise((resolve, reject) => {
    const wait = () => {
      if (fs.existsSync(output) && fs.statSync(output).size > 0) {
        const text = fs.readFileSync(output, 'utf8')
        if (text.includes('END OF TWEAKS INSTALLATION')) return resolve()
      }
      setTimeout(wait, 20)
    }
    setTimeout(() => reject(new Error('Builder stream timed out')), 3000)
    wait()
  })
  const result = fs.readFileSync(output, 'utf8')
  assert(result.includes('Factory AA touchscreen-only preload'))
  assert(result.includes('END OF TWEAKS INSTALLATION'))
  assert(result.includes('get_cmu_sw_version'))
  assert(result.includes(mode === 'install' ? 'FAA_EXPECTED_SHA256=' : 'FAA_MATCH='))
  if (mode === 'install') {
    const destination = path.join(dir, 'config/factory-aa-touch')
    fs.mkdirSync(destination, { recursive: true })
    fs.copyFileSync(builddir + 'factory-aa-touch/libmazda_touch.so', path.join(destination, 'libmazda_touch.so'))
    assert(fs.statSync(path.join(destination, 'libmazda_touch.so')).size > 0)
  }
  console.log('PASS: actual MZD-AIO addTweak/writeTweaksFile functions, ' + mode)
  fs.rmSync(dir, { recursive: true, force: true })
}
Promise.resolve().then(() => run('install')).then(() => run('uninstall')).catch(err => {
  console.error(err)
  process.exitCode = 1
})
