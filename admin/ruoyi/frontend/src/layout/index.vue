<template>
  <div :class="classObj" class="app-wrapper" :style="{ '--current-color': theme }">
    <div v-if="device === 'mobile' && sidebar.opened" class="drawer-bg" @click="handleClickOutside" />
    <side-bar v-if="showSidebar" class="sidebar-container" />
    <div :class="{ hasTagsView: needTagsView, sidebarHide: sidebar.hide }" class="main-container">
      <div :class="{ 'fixed-header': fixedHeader }" class="layout-header">
        <navbar @set-layout="setLayout" />
        <tags-view v-if="needTagsView" />
      </div>
      <app-main :class="{ 'with-fixed-header': fixedHeader, 'with-tags-view': needTagsView }" />
      <settings ref="settingRef" />
      <nav v-if="device === 'mobile' && !sidebar.opened" class="mobile-bottom-nav" aria-label="主要导航">
        <router-link v-for="item in mobileNavigation" :key="item.path" :to="item.path" :aria-current="currentPath === item.path ? 'page' : undefined"><svg-icon :icon-class="item.icon" /><span>{{ item.label }}</span></router-link>
      </nav>
    </div>
  </div>
</template>

<script setup lang="ts">
import { NavTypeEnum } from '@/enums/NavTypeEnum';
import { useAppStore } from '@/store/modules/app';
import { useSettingsStore } from '@/store/modules/settings';
import { initMessageBox, initPush } from '@/utils/push';
import { AppMain, Navbar, Settings, TagsView } from './components';
import SideBar from './components/Sidebar/index.vue';

const mobileNavigation = [
  { path: '/index', label: '总览', icon: 'dashboard' },
  { path: '/riderguard/map', label: '地图', icon: 'guide' },
  { path: '/riderguard/events', label: '风险事件', icon: 'message' },
  { path: '/riderguard/fleet', label: '骑手', icon: 'user' },
  { path: '/riderguard/policy', label: '运维', icon: 'chart' }
];
const currentRoute = useRoute();
const currentPath = computed(() => currentRoute.path);
watch(currentPath, () => { if (useAppStore().device === 'mobile') useAppStore().closeSideBar({ withoutAnimation: false }); });
const settingsStore = useSettingsStore();
const theme = computed(() => settingsStore.theme);
const sidebar = computed(() => useAppStore().sidebar);
const device = computed(() => useAppStore().device);
const needTagsView = computed(() => settingsStore.tagsView);
const fixedHeader = computed(() => settingsStore.fixedHeader);
const layout = computed(() => settingsStore.navType);

// 根据布局模式判断是否显示侧边栏
const showSidebar = computed(() => {
  if (sidebar.value.hide) return false;
  return layout.value === NavTypeEnum.LEFT || layout.value === NavTypeEnum.MIX;
});

const classObj = computed(() => ({
  hideSidebar: !sidebar.value.opened,
  openSidebar: sidebar.value.opened,
  withoutAnimation: sidebar.value.withoutAnimation,
  mobile: device.value === 'mobile'
}));

const { width } = useWindowSize();
const WIDTH = 992; // refer to Bootstrap's responsive design

watch(
  width,
  w => {
    if (w - 1 < WIDTH) {
      useAppStore().toggleDevice('mobile');
      useAppStore().closeSideBar({ withoutAnimation: true });
    } else {
      useAppStore().toggleDevice('desktop');
    }
  },
  { immediate: true }
);

const settingRef = ref<InstanceType<typeof Settings>>();

onMounted(async () => {
  try {
    await initMessageBox();
  } finally {
    initPush();
  }
});

const handleClickOutside = () => {
  useAppStore().closeSideBar({ withoutAnimation: false });
};

const setLayout = () => {
  settingRef.value?.openSetting();
};
</script>

<style lang="scss" scoped>
@use '@/assets/styles/mixin.scss';
@use '@/assets/styles/tokens/sass-vars' as *;

.app-wrapper {
  @include mixin.clearfix;
  position: relative;
  height: 100%;
  width: 100%;
  background: var(--app-shell-bg);

  &.mobile.openSidebar {
    position: fixed;
    top: 0;
  }
}

.drawer-bg {
  background: #000;
  opacity: 0.4;
  width: 100%;
  top: 0;
  height: 100%;
  position: absolute;
  z-index: 999;
}

.layout-header {
  position: relative;
  z-index: 9;
  display: flex;
  flex-direction: column;
  gap: 3px;
  padding: 12px 12px 0;
  background: transparent;
}

.fixed-header {
  position: fixed;
  top: 0;
  right: 0;
  width: calc(100% - #{$base-sidebar-width} - 12px);
  transition: width 0.28s;
}

.hideSidebar .fixed-header {
  width: calc(100% - 70px);
}

.sidebarHide .fixed-header {
  width: 100%;
}

.mobile .fixed-header {
  width: 100%;
  top: 0;
}
.mobile-bottom-nav { position: fixed; inset: auto 0 0; z-index: 1000; display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); padding: 6px 8px calc(6px + env(safe-area-inset-bottom, 0px)); border-top: 1px solid #e4e9e7; background: #fffffff5; box-shadow: 0 -5px 24px #20322a08; }
.mobile-bottom-nav a { display: flex; flex-direction: column; justify-content: center; align-items: center; gap: 5px; min-height: 48px; border-radius: 10px; color: #68777d; font-size: 11px; text-decoration: none; }
.mobile-bottom-nav :deep(.svg-icon) { width: 20px; height: 20px; }
.mobile-bottom-nav a.router-link-exact-active { background: #fff0e7; color: #b74e23; font-weight: 700; }
.mobile .layout-header { padding: calc(6px + env(safe-area-inset-top, 0px)) 8px 0; }
.mobile :deep(.app-main) { padding-left: 8px; padding-right: 8px; padding-bottom: calc(80px + env(safe-area-inset-bottom, 0px)); }
.mobile :deep(.app-main.with-fixed-header) { padding-top: calc(70px + env(safe-area-inset-top, 0px)); }
</style>
