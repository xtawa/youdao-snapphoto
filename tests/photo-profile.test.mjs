import test from 'node:test'
import assert from 'node:assert/strict'
import { CameraBridge, normalizePhotoProfiles, CameraUnavailableError } from '../src/platform/camera-bridge.js'
import { normalizePreferences, DEFAULTS } from '../src/utils/preferences.js'
import { PHOTO_PROFILES, FILTERS } from '../src/utils/format.js'

const PROFILES = [
  {id:'wide', width:3264, height:979},
  {id:'cinema', width:3264, height:1362},
  {id:'two-one', width:3264, height:1632},
  {id:'sixteen-nine', width:3264, height:1836}
]

test('normalizePhotoProfiles drops invalid entries and keeps landscape only', () => {
  const out = normalizePhotoProfiles([
    ...PROFILES,
    {id:'portrait', width:720, height:2160},
    {id:'', width:10, height:5},
    {id:'no-dimensions'},
    'junk'
  ])
  assert.deepEqual(out.map(p => p.id), ['wide','cinema','two-one','sixteen-nine'])
})

test('setPhotoProfile applies only advertised profiles', async () => {
  const calls = []
  const b = new CameraBridge({
    getCapabilities: () => ({camera:true, photoProfiles:PROFILES, filters:['none']}),
    setPhotoProfile: id => calls.push(id)
  })
  await b.connect()
  const profile = await b.setPhotoProfile('cinema')
  assert.equal(profile.width, 3264)
  assert.equal(profile.height, 1362)
  assert.deepEqual(calls, ['cinema'])
  await assert.rejects(b.setPhotoProfile('square'), CameraUnavailableError)
  assert.deepEqual(calls, ['cinema'])
})

test('setPhotoProfile is a no-op when the device has no profile support', async () => {
  const b = new CameraBridge({
    getCapabilities: () => ({camera:true, filters:['none']})
  })
  await b.connect()
  assert.equal(await b.setPhotoProfile('wide'), null)
})

test('photo profile preference round-trip and invalid fallback', () => {
  const normalized = normalizePreferences({photoProfile:'cinema'})
  assert.equal(normalized.photoProfile, 'cinema')
  assert.equal(normalizePreferences({photoProfile:'nope'}).photoProfile, 'wide')
  assert.equal(normalizePreferences({}).photoProfile, DEFAULTS.photoProfile)
})

test('profile metadata matches native table and stays landscape', () => {
  for (const p of PHOTO_PROFILES) {
    assert.ok(p.width >= p.height, p.id + ' must not be portrait')
    assert.ok(p.width <= 3264)
    assert.ok(p.height <= 2448, 'crop must stay inside the full 3264x2448 stream')
  }
})
test('four-three uses the full 8MP sensor and square retains the full sensor height',()=>{
  const full=PHOTO_PROFILES.find(p=>p.id==='four-three'),square=PHOTO_PROFILES.find(p=>p.id==='square')
  assert.equal(full.width,3264);assert.equal(full.height,2448);assert.equal(square.width,2448);assert.equal(square.height,2448)
})

test('every advertised native filter has a UI label', () => {
  const ids = FILTERS.map(f => f.id)
  for (const id of ['none','mono','sepia','warm','cool','vintage','vivid','negative']) {
    assert.ok(ids.includes(id), 'missing label for ' + id)
  }
})
