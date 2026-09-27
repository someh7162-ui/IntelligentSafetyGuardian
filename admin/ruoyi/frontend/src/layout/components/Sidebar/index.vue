<template>
  <div class="sidebar-shell" :class="{ 'has-logo': showLogo }" :style="menuStyle">
    <logo v-if="showLogo" :collapse="isCollapse" />
    <el-scrollbar :class="sideTheme" wrap-class="scrollbar-wrapper">
      <transition :enter-active-class="animateConfig.menuSearchAnimate.enter" mode="out-in">
        <el-menu
          :default-active="activeMenu"
          :collapse="isCollapse"
          :unique-opened="true"
          :collapse-transition="false"
          :popper-offset="12"
          mode="vertical"
        >
          <span v-if="!isCollapse" class="nav-section-label">安全运营</span>
          <sidebar-item v-for="r in operationRoutes" :key="r.path" :item="r" :base-path="r.path" />
          <span v-if="adminRoutes.length && !isCollapse" class="nav-section-label admin-label">管理与维护</span>
          <sidebar-item v-for="r in adminRoutes" :key="r.path" :item="r" :base-path="r.path" />
        </el-menu>
      </transition>
    </el-scrollbar>
    <div v-if="!isCollapse" class="sidebar-footer"><b>RiderGuard</b><span>骑手安全运营平台</span></div>
  </div>
</template>

<script setup lang="ts">
import { RouteRecordRaw } from 'vue-router';
import animateConfig from '@/animate';
import { useAppStore } from '@/store/modules/app';
import { usePermissionStore } from '@/store/modules/permission';
import { useSettingsStore } from '@/store/modules/settings';
import Logo from './Logo.vue';
import SidebarItem from './SidebarItem.vue';

const route = useRoute();
const appStore = useAppStore();
const settingsStore = useSettingsStore();
const permissionStore = usePermissionStore();
const sidebarRouters = computed<RouteRecordRaw[]>(() => permissionStore.getSidebarRoutes());
const adminRoutes = computed(() => sidebarRouters.value.filter(r => ['/system', '/monitor'].includes(r.path.toLowerCase())));
const operationRoutes = computed(() => sidebarRouters.value.filter(r => !adminRoutes.value.includes(r)));
const showLogo = computed(() => settingsStore.sidebarLogo);
const sideTheme = computed(() => settingsStore.sideTheme);
const theme = computed(() => settingsStore.theme);
const isCollapse = computed(() => !appStore.sidebar.opened);

const activeMenu = computed(() => {
  const { meta, path } = route;
  // if set path, the sidebar will highlight the path you set
  if (meta.activeMenu) {
    return meta.activeMenu;
  }
  return path;
});

const bgColor = computed(() => (sideTheme.value === 'theme-dark' ? '#171a20' : '#ffffff'));
const textColor = computed(() => (sideTheme.value === 'theme-dark' ? '#aeb5bb' : '#1f2937'));
const menuStyle = computed(() => ({
  backgroundColor: bgColor.value,
  '--el-menu-bg-color': bgColor.value,
  '--el-menu-text-color': textColor.value,
  '--el-menu-active-color': theme.value
}));
</script>

<style lang="scss" scoped>
.nav-section-label { display: block; padding: 16px 16px 8px; color: #8f9aa5; font-size: 11px; letter-spacing: .08em; }
.admin-label { margin-top: 24px; border-top: 1px solid #ffffff10; }
.sidebar-footer { display: grid; gap: 5px; padding: 14px 16px 4px; border-top: 1px solid #ffffff12; font-size: 11px; color: #94a0aa; }
.sidebar-footer b { color: #d2d8df; font-weight: 550; }
.sidebar-shell {
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 10px 8px 12px;
  border: 1px solid var(--app-sidebar-border);
  border-radius: var(--app-radius-base);
  box-shadow: var(--app-shadow-sm);
  background: v-bind(bgColor) !important;
  overflow: hidden;
}

:deep(.el-scrollbar__view) {
  min-height: 0;
  padding-bottom: 12px;
}

:deep(.el-scrollbar) {
  flex: 1;
  min-height: 0;
  height: auto !important;
}

:deep(.el-scrollbar__wrap) {
  height: 100%;
  overflow-x: hidden;
}
</style>
