import { createApp } from 'vue'

const App = {
  template: `
    <main style="font-family: sans-serif; max-width: 900px; margin: 48px auto">
      <h1>骑手安全守护后台</h1>
      <p>管理端骨架已创建，下一步接入地图、设备和风险事件接口。</p>
    </main>
  `,
}

createApp(App).mount('#app')

