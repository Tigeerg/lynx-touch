// Service subsystem: the service screens (machine INI parameters). The checks hold the
// contract the operator meets: the section navigation, the write lock and the password,
// saving and the fallback of a screen change to the whole machine (through dispatch).
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import { SERVICE_PASSWORD, SYSTEM_PARAMS } from './service-data'
import { serviceReducer, type ServiceAction } from './service'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: ServiceAction[]) =>
  actions.reduce((s, a) => serviceReducer(s, a, reducer).s, state)

const withAuth = (input: string): MachineState => ({
  ...initialState,
  service: { ...initialState.service, auth: { input, error: false } },
})

describe('служба параметров: разделы, запись, сохранение', () => {
  it('выбор раздела переводит на навигацию и сбрасывает курсор', () => {
    const r = serviceReducer(
      { ...initialState, cursor: { ...initialState.cursor, service: 5 } },
      { type: 'SERVICE_SECTION', section: 'joints' },
      reducer,
    )

    expect(r.s.service.section).toBe('joints')
    expect(r.s.service.focus).toBe('nav')
    expect(r.s.cursor.service).toBe(0)
  })

  it('вход в раздел с параметрами переводит к списку', () => {
    const s = run(initialState, { type: 'SERVICE_SECTION', section: 'joints' }, { type: 'SERVICE_ENTER' })

    expect(s.service.focus).toBe('list')
  })

  it('назад из списка возвращает к навигации', () => {
    const inList = run(initialState, { type: 'SERVICE_SECTION', section: 'joints' }, { type: 'SERVICE_ENTER' })
    const back = run(inList, { type: 'SERVICE_BACK' })

    expect(back.service.focus).toBe('nav')
  })

  it('запись закрыта — открывается запрос пароля', () => {
    const r = serviceReducer(initialState, { type: 'SERVICE_LOCK' }, reducer)

    expect(r.s.service.auth).not.toBeNull()
    expect(r.s.service.write).toBe(false)
  })

  it('пароль набирается, неверный помечается ошибкой', () => {
    const typed = run(initialState, { type: 'SERVICE_LOCK' }, { type: 'SERVICE_AUTH_INPUT', value: '12' })
    expect(typed.service.auth?.input).toBe('12')

    const wrong = serviceReducer(withAuth('0000'), { type: 'SERVICE_ENTER' }, reducer)
    expect(wrong.s.service.auth?.error).toBe(true)
    expect(wrong.s.service.write).toBe(false)
  })

  it('верный пароль разрешает запись', () => {
    const r = serviceReducer(withAuth(SERVICE_PASSWORD), { type: 'SERVICE_ENTER' }, reducer)

    expect(r.s.service.write).toBe(true)
    expect(r.s.service.auth).toBeNull()
  })

  it('сохранение без разрешения записи отклонено', () => {
    const r = serviceReducer(initialState, { type: 'SERVICE_SAVE' }, reducer)

    expect(r.msg).toContain('Запись заблокирована')
  })

  it('раздел доезжает до главного редьюсера', () => {
    const s = reducer(initialState, { type: 'SERVICE_SECTION', section: 'joints' })

    expect(s.service.section).toBe('joints')
  })
})

describe('служба параметров: ключа нет в состоянии', () => {
  const editing = (id: string, original: number): MachineState => ({
    ...initialState,
    params: {},
    service: { ...initialState.service, focus: 'list', editing: { id, original } },
  })

  it('правка списка берёт значение по умолчанию, а не NaN', () => {
    const option = SYSTEM_PARAMS[0]
    const s = run(editing(option.id, option.default), { type: 'PARAM_ADJUST', dir: 1 })

    expect(s.params[option.id]).toBe(1)
  })

  it('правка числа считает от значения по умолчанию', () => {
    const index = SYSTEM_PARAMS.findIndex((d) => !d.options)
    const numeric = SYSTEM_PARAMS[index]
    const s = run(
      { ...editing(numeric.id, numeric.default), cursor: { ...initialState.cursor, service: index } },
      { type: 'PARAM_ADJUST', dir: 1 },
    )

    expect(Number.isFinite(s.params[numeric.id])).toBe(true)
    expect(s.params[numeric.id]).toBe(Math.min(numeric.max, numeric.default + numeric.step))
  })
})
